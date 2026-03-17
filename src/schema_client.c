#include "schema_client.h"
#include "nl_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* ── EVO protocol helpers ────────────────────────────────────────────── */

/*
 * EVO text protocol (TCP:9967) handshake:
 *   Client → "EVO\n"
 *   Server → "HELLO EvoSQL 1.0\n"
 *   (If TLS available: Server → "STARTTLS\n", Client → "NOTLS\n")
 *   Server → "AUTH_REQUIRED\n"
 *   Client → "AUTH <user> <password>\n"
 *   Server → "AUTH_OK\n"
 *
 * SQL commands:
 *   Client → "SQL <length>\n"
 *   Client → "<sql>\n"
 *   Server → response lines...
 *   Server → "READY\n"
 */

#define EVO_RECV_BUF  (256 * 1024)
#define EVO_TIMEOUT_S 10

static int set_recv_timeout(int sock, int seconds)
{
    struct timeval tv = { .tv_sec = seconds, .tv_usec = 0 };
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

static int evo_send(int sock, const char *msg)
{
    size_t len = strlen(msg);
    ssize_t sent = send(sock, msg, len, 0);
    return (sent == (ssize_t)len) ? 0 : -1;
}

/* Read a single line (up to '\n'). Returns length, -1 on error. */
static int evo_recv_line(int sock, char *buf, int max)
{
    int i = 0;
    while (i < max - 1) {
        char c;
        ssize_t n = recv(sock, &c, 1, 0);
        if (n <= 0) return -1;
        if (c == '\n') break;
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

/*
 * Parsed result from EVO protocol structured response.
 * EVO format:
 *   RESULT\n
 *   COLS <n>\n
 *   COL <name>\n   (× n)
 *   ROW\n
 *   FIELD <value>\n (× n per row)
 *   END\n
 *   TAG <message>\n
 *   READY\n
 */
typedef struct {
    int    n_cols;
    char **col_names;        /* column headers */
    int    n_rows;
    char ***rows;            /* rows[row_idx][col_idx] */
    char   tag[256];         /* TAG message (e.g. "SELECT 3") */
} EvoResult;

static void evo_result_free(EvoResult *r)
{
    if (!r) return;
    for (int i = 0; i < r->n_cols; i++) free(r->col_names[i]);
    free(r->col_names);
    for (int i = 0; i < r->n_rows; i++) {
        for (int j = 0; j < r->n_cols; j++) free(r->rows[i][j]);
        free(r->rows[i]);
    }
    free(r->rows);
}

/* Read EVO structured response until READY. Parses into EvoResult. */
static int evo_read_result(int sock, EvoResult *out)
{
    memset(out, 0, sizeof(*out));
    char line[65536];
    int col_cap = 0, row_cap = 0, cur_col = 0;

    while (1) {
        int len = evo_recv_line(sock, line, sizeof(line));
        if (len < 0) return -1;

        if (strcmp(line, "READY") == 0)
            break;
        else if (strcmp(line, "RESULT") == 0)
            continue;   /* start of result set */
        else if (strncmp(line, "COLS ", 5) == 0) {
            out->n_cols = atoi(line + 5);
            if (out->n_cols > 0) {
                col_cap = out->n_cols;
                out->col_names = calloc(col_cap, sizeof(char *));
            }
        }
        else if (strncmp(line, "COL ", 4) == 0) {
            if (cur_col < col_cap)
                out->col_names[cur_col++] = strdup(line + 4);
        }
        else if (strcmp(line, "ROW") == 0) {
            /* new row — allocate space */
            if (out->n_rows >= row_cap) {
                row_cap = row_cap ? row_cap * 2 : 16;
                out->rows = realloc(out->rows, row_cap * sizeof(char **));
            }
            out->rows[out->n_rows] = calloc(out->n_cols > 0 ? out->n_cols : 1, sizeof(char *));
            out->n_rows++;
            cur_col = 0;  /* reset field counter per row */
        }
        else if (strncmp(line, "FIELD ", 6) == 0) {
            if (out->n_rows > 0 && cur_col < out->n_cols) {
                out->rows[out->n_rows - 1][cur_col++] = strdup(line + 6);
            }
        }
        else if (strcmp(line, "END") == 0)
            continue;
        else if (strncmp(line, "TAG ", 4) == 0)
            snprintf(out->tag, sizeof(out->tag), "%s", line + 4);
        else if (strncmp(line, "OK", 2) == 0 || strncmp(line, "ERR", 3) == 0)
            snprintf(out->tag, sizeof(out->tag), "%s", line);
        /* else: ignore unknown lines */
    }

    return 0;
}

/* ── Public API ──────────────────────────────────────────────────────── */

int evo_connect(const char *host, int port, const char *user, const char *password)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    set_recv_timeout(sock, EVO_TIMEOUT_S);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(host);
        if (!he) { close(sock); return -1; }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    char line[1024];

    /* Step 1: Send EVO greeting */
    if (evo_send(sock, "EVO\n") < 0) {
        close(sock);
        return -1;
    }

    /* Step 2: Expect "HELLO EvoSQL ..." */
    if (evo_recv_line(sock, line, sizeof(line)) < 0 ||
        strncmp(line, "HELLO", 5) != 0) {
        fprintf(stderr, "[schema_client] bad greeting: %s\n", line);
        close(sock);
        return -1;
    }

    /* Step 3: Check for STARTTLS offer */
    if (evo_recv_line(sock, line, sizeof(line)) < 0) {
        close(sock);
        return -1;
    }

    if (strcmp(line, "STARTTLS") == 0) {
        /* Decline TLS */
        evo_send(sock, "NOTLS\n");
        /* Read AUTH_REQUIRED */
        if (evo_recv_line(sock, line, sizeof(line)) < 0) {
            close(sock);
            return -1;
        }
    }
    /* line should now be "AUTH_REQUIRED" */

    if (strncmp(line, "AUTH_REQUIRED", 13) != 0) {
        fprintf(stderr, "[schema_client] expected AUTH_REQUIRED, got: %s\n", line);
        close(sock);
        return -1;
    }

    /* Step 4: Send AUTH */
    char auth_cmd[512];
    snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s %s\n", user, password);
    if (evo_send(sock, auth_cmd) < 0) {
        close(sock);
        return -1;
    }

    /* Step 5: Expect AUTH_OK */
    if (evo_recv_line(sock, line, sizeof(line)) < 0 ||
        strncmp(line, "AUTH_OK", 7) != 0) {
        fprintf(stderr, "[schema_client] auth failed: %s\n", line);
        close(sock);
        return -1;
    }

    return sock;
}

/* Send SQL and collect raw response until READY (for evo_query compat) */
static char *evo_read_until_ready(int sock)
{
    char *buf = malloc(EVO_RECV_BUF);
    if (!buf) return NULL;
    int total = 0;
    char line[65536];
    while (total < EVO_RECV_BUF - 1) {
        int len = evo_recv_line(sock, line, sizeof(line));
        if (len < 0) break;
        if (strcmp(line, "READY") == 0) break;
        if (total + len + 2 >= EVO_RECV_BUF) break;
        memcpy(buf + total, line, len);
        total += len;
        buf[total++] = '\n';
    }
    buf[total] = '\0';
    return buf;
}

static int evo_send_sql(int sock, const char *sql)
{
    int sql_len = (int)strlen(sql);
    char header[128];
    snprintf(header, sizeof(header), "SQL %d\n", sql_len);
    if (evo_send(sock, header) < 0) return -1;
    char *body = malloc(sql_len + 2);
    if (!body) return -1;
    snprintf(body, sql_len + 2, "%s\n", sql);
    int rc = evo_send(sock, body);
    free(body);
    return rc;
}

char *evo_query(int sock, const char *sql)
{
    if (sock < 0 || !sql) return NULL;
    if (evo_send_sql(sock, sql) < 0) return NULL;
    return evo_read_until_ready(sock);
}

/* Send SQL and parse structured EVO response */
static int evo_query_parsed(int sock, const char *sql, EvoResult *out)
{
    if (sock < 0 || !sql) return -1;
    if (evo_send_sql(sock, sql) < 0) return -1;
    return evo_read_result(sock, out);
}

void evo_disconnect(int sock)
{
    if (sock >= 0) {
        evo_send(sock, "QUIT\n");
        char line[256];
        evo_recv_line(sock, line, sizeof(line)); /* BYE */
        close(sock);
    }
}

/* ── Schema fetcher ──────────────────────────────────────────────────── */

/*
 * Map data_type string from information_schema to SQL DDL type.
 * E.g., "character varying" → "VARCHAR", "integer" → "INT"
 */
static const char *map_type(const char *data_type)
{
    if (!data_type) return "TEXT";
    if (strcasecmp(data_type, "integer") == 0) return "INT";
    if (strcasecmp(data_type, "bigint") == 0) return "BIGINT";
    if (strcasecmp(data_type, "character varying") == 0) return "VARCHAR";
    if (strcasecmp(data_type, "text") == 0) return "TEXT";
    if (strcasecmp(data_type, "boolean") == 0) return "BOOLEAN";
    if (strcasecmp(data_type, "date") == 0) return "DATE";
    if (strcasecmp(data_type, "double precision") == 0) return "DOUBLE";
    if (strcasecmp(data_type, "real") == 0) return "FLOAT";
    if (strcasecmp(data_type, "numeric") == 0) return "DECIMAL";
    if (strcasecmp(data_type, "uuid") == 0) return "UUID";
    return data_type;  /* pass through unknown types */
}

/* Find column index by name in EvoResult */
static int find_col(const EvoResult *r, const char *name)
{
    for (int i = 0; i < r->n_cols; i++) {
        if (r->col_names[i] && strcasecmp(r->col_names[i], name) == 0)
            return i;
    }
    return -1;
}

/* Safe field getter */
static const char *get_field(const EvoResult *r, int row, int col)
{
    if (col < 0 || col >= r->n_cols || row < 0 || row >= r->n_rows)
        return NULL;
    return r->rows[row][col];
}

int schema_fetch(NLSession *session, const NLConfig *cfg)
{
    if (!session || !cfg) return -1;

    int sock = evo_connect(cfg->evo_host, cfg->evo_port,
                           cfg->evo_user, cfg->evo_password);
    if (sock < 0) {
        fprintf(stderr, "[schema_client] cannot connect to EvoSQL %s:%d\n",
                cfg->evo_host, cfg->evo_port);
        return -1;
    }

    /* ── Step 1: Get table list via SHOW TABLES ────────────────────── */
    EvoResult tables_res;
    if (evo_query_parsed(sock, "SHOW TABLES", &tables_res) < 0) {
        evo_disconnect(sock);
        return -1;
    }

    /* SHOW TABLES returns single-column result with table names */
    int table_count = tables_res.n_rows;
    char **table_names = calloc(table_count + 1, sizeof(char *));
    for (int i = 0; i < table_count; i++) {
        if (tables_res.rows[i] && tables_res.rows[i][0])
            table_names[i] = strdup(tables_res.rows[i][0]);
    }
    evo_result_free(&tables_res);

    /* ── Step 2: Get ALL column info in one query ──────────────────── */
    /*
     * information_schema.columns returns ALL columns for ALL tables.
     * EvoSQL ignores WHERE clause on virtual tables, so we get everything
     * and filter client-side by table_name.
     *
     * Returned columns vary but include:
     *   table_name, column_name, data_type, is_nullable, column_default, ...
     * We use column headers to find the right indices.
     */
    EvoResult col_res;
    int have_cols = (evo_query_parsed(sock,
        "SELECT table_name, column_name, data_type, is_nullable "
        "FROM information_schema.columns", &col_res) == 0);

    int ci_tbl  = have_cols ? find_col(&col_res, "table_name")   : -1;
    int ci_col  = have_cols ? find_col(&col_res, "column_name")  : -1;
    int ci_type = have_cols ? find_col(&col_res, "data_type")    : -1;
    int ci_null = have_cols ? find_col(&col_res, "is_nullable")  : -1;

    /* ── Step 3: Build DDL-style schema text ───────────────────────── */
    size_t cap = 4096;
    char *schema = malloc(cap);
    if (!schema) {
        if (have_cols) evo_result_free(&col_res);
        for (int i = 0; i < table_count; i++) free(table_names[i]);
        free(table_names);
        evo_disconnect(sock);
        return -1;
    }
    int slen = 0;

    slen += snprintf(schema + slen, cap - slen,
                     "DATABASE: %s\n",
                     session->evo_database[0] ? session->evo_database : "default");

    for (int t = 0; t < table_count; t++) {
        if (!table_names[t]) continue;

        size_t needed = strlen(table_names[t]) + 256;
        if ((size_t)(slen + needed) >= cap) {
            cap = (slen + needed) * 2;
            schema = realloc(schema, cap);
        }

        slen += snprintf(schema + slen, cap - slen, "TABLE %s (", table_names[t]);

        int first_col = 1;
        if (have_cols && ci_tbl >= 0 && ci_col >= 0 && ci_type >= 0) {
            for (int r = 0; r < col_res.n_rows; r++) {
                const char *tbl = get_field(&col_res, r, ci_tbl);
                if (!tbl || strcasecmp(tbl, table_names[t]) != 0)
                    continue;

                const char *col_name  = get_field(&col_res, r, ci_col);
                const char *data_type = get_field(&col_res, r, ci_type);
                const char *nullable  = ci_null >= 0 ? get_field(&col_res, r, ci_null) : "YES";

                if (!col_name) continue;

                if (!first_col)
                    slen += snprintf(schema + slen, cap - slen, ", ");
                first_col = 0;

                slen += snprintf(schema + slen, cap - slen, "%s %s",
                                 col_name, map_type(data_type));

                if (nullable && strcasecmp(nullable, "NO") == 0)
                    slen += snprintf(schema + slen, cap - slen, " NOT NULL");

                /* ensure buffer capacity */
                if ((size_t)(slen + 256) >= cap) {
                    cap *= 2;
                    schema = realloc(schema, cap);
                }
            }
        }

        slen += snprintf(schema + slen, cap - slen, ");\n");
        free(table_names[t]);
    }
    free(table_names);
    if (have_cols) evo_result_free(&col_res);

    evo_disconnect(sock);

    free(session->schema_text);
    session->schema_text = schema;
    session->schema_len = slen;

    fprintf(stderr, "[schema_client] fetched schema (%d bytes): %s\n", slen, schema);

    return 0;
}

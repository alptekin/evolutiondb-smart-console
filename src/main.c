#include "nl_service.h"
#include "schema_client.h"
#include "sql_validator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ── Globals ─────────────────────────────────────────────────────────── */

static volatile int g_running = 1;
static NLConfig     g_config;

/* ── Signal handler ──────────────────────────────────────────────────── */

static void sighandler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ── Session line reader ─────────────────────────────────────────────── */

static int read_line(int sock, char *buf, int max)
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

static int send_line(int sock, const char *msg)
{
    size_t len = strlen(msg);
    char *buf = malloc(len + 2);
    memcpy(buf, msg, len);
    buf[len] = '\n';
    buf[len + 1] = '\0';
    ssize_t sent = send(sock, buf, len + 1, 0);
    free(buf);
    return (sent > 0) ? 0 : -1;
}

/* ── Parse PROVIDER command ──────────────────────────────────────────── */

static int parse_provider_cmd(const char *args, ProviderConfig *cfg)
{
    /* PROVIDER <type> [model] [api_key] [base_url] */
    char type_str[64] = {0};
    char model[256] = {0};
    char api_key[512] = {0};
    char base_url[512] = {0};

    /* parse space-separated fields */
    const char *p = args;
    while (*p == ' ') p++;

    /* type */
    int i = 0;
    while (*p && *p != ' ' && i < 63) type_str[i++] = *p++;
    type_str[i] = '\0';
    while (*p == ' ') p++;

    /* model */
    i = 0;
    while (*p && *p != ' ' && i < 255) model[i++] = *p++;
    model[i] = '\0';
    while (*p == ' ') p++;

    /* api_key */
    i = 0;
    while (*p && *p != ' ' && i < 511) api_key[i++] = *p++;
    api_key[i] = '\0';
    while (*p == ' ') p++;

    /* base_url */
    i = 0;
    while (*p && *p != ' ' && i < 511) base_url[i++] = *p++;
    base_url[i] = '\0';

    /* resolve type */
    if (strcasecmp(type_str, "OLLAMA") == 0)
        cfg->type = PROVIDER_OLLAMA;
    else if (strcasecmp(type_str, "CLAUDE") == 0)
        cfg->type = PROVIDER_CLAUDE;
    else if (strcasecmp(type_str, "OPENAI") == 0)
        cfg->type = PROVIDER_OPENAI;
    else
        return -1;

    /* set fields (use defaults if dash "-" or empty) */
    if (model[0] && strcmp(model, "-") != 0)
        strncpy(cfg->model, model, sizeof(cfg->model) - 1);
    else
        strncpy(cfg->model, provider_default_model(cfg->type), sizeof(cfg->model) - 1);

    if (api_key[0] && strcmp(api_key, "-") != 0)
        strncpy(cfg->api_key, api_key, sizeof(cfg->api_key) - 1);

    if (base_url[0] && strcmp(base_url, "-") != 0)
        strncpy(cfg->base_url, base_url, sizeof(cfg->base_url) - 1);
    else
        strncpy(cfg->base_url, provider_default_url(cfg->type), sizeof(cfg->base_url) - 1);

    return 0;
}

/* ── Client session handler ──────────────────────────────────────────── */

static void *handle_client(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);

    /* greeting */
    send_line(client_fd, "NL_READY");

    NLSession *session = nl_session_create();
    if (!session) {
        send_line(client_fd, "ERROR failed to create session");
        close(client_fd);
        return NULL;
    }

    /* set default provider from config */
    session->provider = g_config.default_provider;

    /* try to fetch schema from EvoSQL */
    if (schema_fetch(session, &g_config) == 0) {
        fprintf(stderr, "[session] schema fetched (%d bytes)\n", session->schema_len);
    } else {
        fprintf(stderr, "[session] warning: could not fetch schema from EvoSQL\n");
    }

    char line[65536];
    while (g_running) {
        int len = read_line(client_fd, line, sizeof(line));
        if (len < 0) break;
        if (len == 0) continue;

        /* ── QUIT ─────────────────────────────────────────────── */
        if (strcasecmp(line, "QUIT") == 0) {
            send_line(client_fd, "BYE");
            break;
        }

        /* ── PROVIDER ─────────────────────────────────────────── */
        if (strncasecmp(line, "PROVIDER ", 9) == 0) {
            ProviderConfig new_cfg;
            memset(&new_cfg, 0, sizeof(new_cfg));
            if (parse_provider_cmd(line + 9, &new_cfg) == 0) {
                session->provider = new_cfg;
                char resp[512];
                const char *type_name = "ollama";
                if (new_cfg.type == PROVIDER_CLAUDE) type_name = "claude";
                else if (new_cfg.type == PROVIDER_OPENAI) type_name = "openai";
                snprintf(resp, sizeof(resp), "OK provider=%s model=%s",
                         type_name, new_cfg.model);
                send_line(client_fd, resp);
            } else {
                send_line(client_fd, "ERROR invalid provider. Use: OLLAMA, CLAUDE, OPENAI");
            }
            continue;
        }

        /* ── SCHEMA ───────────────────────────────────────────── */
        if (strcasecmp(line, "SCHEMA") == 0) {
            if (!session->schema_text) {
                schema_fetch(session, &g_config);
            }
            if (session->schema_text) {
                char *resp = malloc(session->schema_len * 2 + 32);
                int off = sprintf(resp, "SCHEMA_OK ");
                for (int i = 0; i < session->schema_len; i++) {
                    if (session->schema_text[i] == '\n') {
                        resp[off++] = '\\';
                        resp[off++] = 'n';
                    } else {
                        resp[off++] = session->schema_text[i];
                    }
                }
                resp[off] = '\0';
                send_line(client_fd, resp);
                free(resp);
            } else {
                send_line(client_fd, "ERROR cannot fetch schema");
            }
            continue;
        }

        /* ── EXECUTE ──────────────────────────────────────────── */
        if (strcasecmp(line, "EXECUTE") == 0) {
            if (!session->awaiting_confirm || session->pending_sql[0] == '\0') {
                send_line(client_fd, "ERROR no pending SQL to execute");
                continue;
            }

            int evo = evo_connect(g_config.evo_host, g_config.evo_port,
                                  g_config.evo_user, g_config.evo_password);
            if (evo < 0) {
                send_line(client_fd, "ERROR cannot connect to EvoSQL");
                session->awaiting_confirm = 0;
                continue;
            }

            if (session->evo_database[0]) {
                char use_cmd[512];
                snprintf(use_cmd, sizeof(use_cmd), "USE %s", session->evo_database);
                char *r = evo_query(evo, use_cmd);
                free(r);
            }

            char *result = evo_query(evo, session->pending_sql);
            evo_disconnect(evo);

            if (result) {
                char formatted[65536];
                int flen = 0;
                const char *p = result;

                int n_cols = 0;
                char *col_names[64];
                int col_idx = 0;
                int first_row = 1;

                while (*p) {
                    const char *eol = strchr(p, '\n');
                    if (!eol) eol = p + strlen(p);
                    int ll = (int)(eol - p);

                    if (ll >= 5 && strncmp(p, "COLS ", 5) == 0) {
                        n_cols = atoi(p + 5);
                        col_idx = 0;
                    }
                    else if (ll >= 4 && strncmp(p, "COL ", 4) == 0) {
                        if (col_idx < 64 && col_idx < n_cols) {
                            col_names[col_idx] = strndup(p + 4, ll - 4);
                            col_idx++;
                        }
                    }
                    else if (ll == 3 && strncmp(p, "ROW", 3) == 0) {
                        if (!first_row && flen > 0)
                            flen += snprintf(formatted + flen, sizeof(formatted) - flen, "\n");
                        first_row = 0;
                        col_idx = 0;
                    }
                    else if (ll >= 6 && strncmp(p, "FIELD ", 6) == 0) {
                        if (col_idx > 0 && flen > 0)
                            flen += snprintf(formatted + flen, sizeof(formatted) - flen, " | ");
                        if (col_idx < n_cols && col_names[col_idx])
                            flen += snprintf(formatted + flen, sizeof(formatted) - flen,
                                             "%s=%.*s", col_names[col_idx], ll - 6, p + 6);
                        else
                            flen += snprintf(formatted + flen, sizeof(formatted) - flen,
                                             "%.*s", ll - 6, p + 6);
                        col_idx++;
                    }
                    else if (ll >= 4 && strncmp(p, "TAG ", 4) == 0) {
                        if (flen > 0)
                            flen += snprintf(formatted + flen, sizeof(formatted) - flen, "\n");
                        flen += snprintf(formatted + flen, sizeof(formatted) - flen,
                                         "%.*s", ll - 4, p + 4);
                    }
                    else if (ll >= 2 && strncmp(p, "OK", 2) == 0) {
                        flen += snprintf(formatted + flen, sizeof(formatted) - flen,
                                         "%.*s", ll, p);
                    }
                    else if (ll >= 3 && strncmp(p, "ERR", 3) == 0) {
                        flen += snprintf(formatted + flen, sizeof(formatted) - flen,
                                         "%.*s", ll, p);
                    }

                    p = (*eol) ? eol + 1 : eol;
                }

                formatted[flen] = '\0';

                for (int fi = 0; fi < flen; fi++) {
                    if (formatted[fi] == '\n') formatted[fi] = ' ';
                }

                for (int c = 0; c < n_cols && c < 64; c++) free(col_names[c]);

                char *resp = malloc(flen + 32);
                sprintf(resp, "RESULT %s", flen > 0 ? formatted : "OK");
                send_line(client_fd, resp);
                free(resp);
                free(result);
            } else {
                send_line(client_fd, "ERROR query execution failed");
            }

            session->awaiting_confirm = 0;
            session->pending_sql[0] = '\0';
            schema_fetch(session, &g_config);
            continue;
        }

        /* ── SQL <text> (direct SQL, bypass model) ────────────── */
        if (strncasecmp(line, "SQL ", 4) == 0) {
            const char *sql = line + 4;
            while (*sql == ' ') sql++;
            if (strlen(sql) == 0) {
                send_line(client_fd, "ERROR empty SQL");
                continue;
            }
            strncpy(session->pending_sql, sql, sizeof(session->pending_sql) - 1);
            session->awaiting_confirm = 1;
            char resp[65600];
            snprintf(resp, sizeof(resp), "SQL_PROPOSAL %s", sql);
            send_line(client_fd, resp);
            continue;
        }

        /* ── REJECT ───────────────────────────────────────────── */
        if (strcasecmp(line, "REJECT") == 0) {
            session->awaiting_confirm = 0;
            session->pending_sql[0] = '\0';
            send_line(client_fd, "OK");
            continue;
        }

        /* ── DATABASE <name> ──────────────────────────────────── */
        if (strncasecmp(line, "DATABASE ", 9) == 0) {
            strncpy(session->evo_database, line + 9, sizeof(session->evo_database) - 1);
            schema_fetch(session, &g_config);
            send_line(client_fd, "OK");
            continue;
        }

        /* ── NL <text> ────────────────────────────────────────── */
        if (strncasecmp(line, "NL ", 3) == 0) {
            const char *text = line + 3;
            while (*text == ' ') text++;

            if (strlen(text) == 0) {
                send_line(client_fd, "ERROR empty input");
                continue;
            }

            char *response = nl_process_input(session, text);
            if (response) {
                send_line(client_fd, response);
                free(response);
            } else {
                send_line(client_fd, "ERROR inference failed");
            }
            continue;
        }

        /* ── Unknown command ──────────────────────────────────── */
        send_line(client_fd, "ERROR unknown command. Use: NL, SCHEMA, EXECUTE, REJECT, DATABASE, PROVIDER, QUIT");
    }

    nl_session_destroy(session);
    close(client_fd);
    return NULL;
}

/* ── Banner ──────────────────────────────────────────────────────────── */

static void print_banner(void)
{
    const char *type_name = "ollama";
    if (g_config.default_provider.type == PROVIDER_CLAUDE) type_name = "claude";
    else if (g_config.default_provider.type == PROVIDER_OPENAI) type_name = "openai";

    fprintf(stderr,
        "╔══════════════════════════════════════════╗\n"
        "║   EvoSQL NL→SQL Service                  ║\n"
        "║   Port: %-5d                            ║\n"
        "║   Provider: %-8s                     ║\n"
        "║   EvoSQL: %s:%-5d                  ║\n"
        "╚══════════════════════════════════════════╝\n",
        g_config.port, type_name,
        g_config.evo_host, g_config.evo_port);
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    g_config = nl_config_from_env();

    print_banner();

    /* initialize service (curl, etc.) */
    if (nl_service_init() != 0) {
        fprintf(stderr, "ERROR: failed to initialize service\n");
        return 1;
    }

    /* signal handling */
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGPIPE, SIG_IGN);

    /* create TCP server */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        nl_service_cleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_config.port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        nl_service_cleanup();
        return 1;
    }

    if (listen(server_fd, 8) < 0) {
        perror("listen");
        close(server_fd);
        nl_service_cleanup();
        return 1;
    }

    fprintf(stderr, "[server] listening on port %d\n", g_config.port);

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        fprintf(stderr, "[server] client connected: %s\n", client_ip);

        int *fd_ptr = malloc(sizeof(int));
        *fd_ptr = client_fd;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, fd_ptr) != 0) {
            perror("pthread_create");
            free(fd_ptr);
            close(client_fd);
        } else {
            pthread_detach(tid);
        }
    }

    fprintf(stderr, "[server] shutting down\n");
    close(server_fd);
    nl_service_cleanup();

    return 0;
}

/*
 * nl-client.c — EvoSQL Smart Console
 *
 * Interactive CLI: natural language -> SQL -> EvoSQL
 * Features:
 *   - SQL syntax highlighting
 *   - Multi-line SQL formatting
 *   - Built-in SQL editor (Ctrl+E)
 *   - Arrow key navigation
 *   - UTF-8 support
 *   - i18n (English / Turkish)
 *   - LLM provider selection & config persistence
 *   - Direct SQL slash commands (/tables, /schemas, /describe, etc.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <readline/readline.h>
#include <readline/history.h>


#include "i18n.h"
#include "config.h"

/* ═══════════════════════════════════════════════════════════════════════
   Colors and Styles
   ═══════════════════════════════════════════════════════════════════════ */

#define RST   "\033[0m"
#define BOLD  "\033[1m"
#define DIM   "\033[2m"
#define ITAL  "\033[3m"
#define RED   "\033[31m"
#define GRN   "\033[32m"
#define YEL   "\033[33m"
#define BLU   "\033[34m"
#define MAG   "\033[35m"
#define CYN   "\033[36m"
#define WHT   "\033[37m"
#define BRED  "\033[1;31m"
#define BGRN  "\033[1;32m"
#define BYEL  "\033[1;33m"
#define BBLU  "\033[1;34m"
#define BMAG  "\033[1;35m"
#define BCYN  "\033[1;36m"

/* ═══════════════════════════════════════════════════════════════════════
   Key Codes
   ═══════════════════════════════════════════════════════════════════════ */

enum {
    K_ENTER     = 13,
    K_TAB       = 9,
    K_BACKSPACE = 127,
    K_ESCAPE    = 27,
    K_CTRL_A    = 1,
    K_CTRL_C    = 3,
    K_CTRL_E    = 5,
    K_CTRL_O    = 15,
    K_CTRL_S    = 19,
    /* Special keys (>= 0x100) */
    K_UP = 0x100, K_DOWN, K_LEFT, K_RIGHT,
    K_HOME, K_END, K_DELETE, K_PGUP, K_PGDN,
};

/* ═══════════════════════════════════════════════════════════════════════
   Globals
   ═══════════════════════════════════════════════════════════════════════ */

static int   g_sock = -1;
static char  g_db[256] = "testdb";
static char  g_host[256] = "127.0.0.1";
static int   g_port = 9970;
static char  g_histfile[512];
static struct termios g_orig_termios;
static int   g_raw_mode = 0;
static pid_t g_service_pid = 0; /* child nl-service process */

/* UTF-8 multi-byte buffer for read_key() */
#define K_UTF8 0x200
static char  g_utf8_buf[8];
static int   g_utf8_len = 0;

/* i18n pointer — set once in main() */
static const I18nStrings *L = NULL;

/* Provider config — set at startup */
static ClientProviderConfig g_provider;

/* ═══════════════════════════════════════════════════════════════════════
   Terminal Helpers
   ═══════════════════════════════════════════════════════════════════════ */

static void enable_raw_mode(void)
{
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    struct termios raw = g_orig_termios;
    raw.c_iflag &= ~(ICRNL | IXON);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    g_raw_mode = 1;
}

static void disable_raw_mode(void)
{
    if (g_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        g_raw_mode = 0;
    }
}

static int read_key(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;

    /* Escape sequence */
    if (c == 27) {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return K_ESCAPE;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return K_ESCAPE;
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                char s2;
                if (read(STDIN_FILENO, &s2, 1) != 1) return K_ESCAPE;
                if (s2 == '~') {
                    switch (seq[1]) {
                    case '1': return K_HOME;
                    case '3': return K_DELETE;
                    case '4': return K_END;
                    case '5': return K_PGUP;
                    case '6': return K_PGDN;
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A': return K_UP;
                case 'B': return K_DOWN;
                case 'C': return K_RIGHT;
                case 'D': return K_LEFT;
                case 'H': return K_HOME;
                case 'F': return K_END;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H': return K_HOME;
            case 'F': return K_END;
            }
        }
        return K_ESCAPE;
    }

    if (c == 127 || c == 8) return K_BACKSPACE;

    /* UTF-8 multi-byte character */
    if (c >= 0xC0) {
        g_utf8_buf[0] = (char)c;
        int extra = 0;
        if ((c & 0xE0) == 0xC0) extra = 1;      /* 2-byte: 110xxxxx */
        else if ((c & 0xF0) == 0xE0) extra = 2;  /* 3-byte: 1110xxxx */
        else if ((c & 0xF8) == 0xF0) extra = 3;  /* 4-byte: 11110xxx */
        for (int i = 0; i < extra; i++) {
            if (read(STDIN_FILENO, &g_utf8_buf[1 + i], 1) != 1) break;
        }
        g_utf8_len = 1 + extra;
        g_utf8_buf[g_utf8_len] = '\0';
        return K_UTF8;
    }

    return (int)c;
}

static void get_term_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   Network
   ═══════════════════════════════════════════════════════════════════════ */

static int send_line(const char *msg)
{
    size_t len = strlen(msg);
    char *buf = malloc(len + 2);
    memcpy(buf, msg, len);
    buf[len] = '\n';
    ssize_t n = send(g_sock, buf, len + 1, 0);
    free(buf);
    return n > 0 ? 0 : -1;
}

static int recv_line(char *buf, int max, int timeout_sec)
{
    struct timeval tv = { .tv_sec = timeout_sec, .tv_usec = 0 };
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int i = 0;
    while (i < max - 1) {
        char c;
        ssize_t n = recv(g_sock, &c, 1, 0);
        if (n <= 0) return -1;
        if (c == '\n') break;
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

/* ═══════════════════════════════════════════════════════════════════════
   SQL Keyword Recognition
   ═══════════════════════════════════════════════════════════════════════ */

static int is_sql_keyword(const char *word)
{
    static const char *kws[] = {
        "SELECT","FROM","WHERE","INSERT","INTO","UPDATE","DELETE",
        "CREATE","TABLE","DROP","ALTER","VALUES","SET",
        "JOIN","ON","AND","OR","NOT","NULL",
        "PRIMARY","KEY","FOREIGN","REFERENCES","INDEX",
        "AUTO_INCREMENT","DEFAULT","UNIQUE","CHECK","CONSTRAINT",
        "ORDER","BY","GROUP","HAVING","LIMIT","OFFSET",
        "AS","DISTINCT","IN","LIKE","BETWEEN","EXISTS",
        "UNION","ALL","IF","CASCADE","RESTRICT",
        "COUNT","SUM","AVG","MIN","MAX",
        "ASC","DESC","IS","CASE","WHEN","THEN","ELSE","END",
        "LEFT","RIGHT","INNER","OUTER","CROSS","FULL",
        NULL
    };
    for (int i = 0; kws[i]; i++)
        if (strcasecmp(word, kws[i]) == 0) return 1;
    return 0;
}

static int is_sql_type(const char *word)
{
    static const char *types[] = {
        "INT","INTEGER","BIGINT","SMALLINT","TINYINT",
        "VARCHAR","TEXT","CHAR","BOOLEAN","BOOL",
        "DATE","TIMESTAMP","TIME",
        "FLOAT","DOUBLE","DECIMAL","NUMERIC","REAL",
        "UUID","BLOB","SERIAL",
        NULL
    };
    for (int i = 0; types[i]; i++)
        if (strcasecmp(word, types[i]) == 0) return 1;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
   SQL Syntax Highlighting
   ═══════════════════════════════════════════════════════════════════════ */

static void highlight_print(const char *text)
{
    const char *p = text;
    while (*p) {
        /* Whitespace */
        if (*p == ' ' || *p == '\t') {
            putchar(*p++);
            continue;
        }
        /* String literal */
        if (*p == '\'') {
            printf(GRN);
            putchar(*p++);
            while (*p && *p != '\'') {
                if (*p == '\'' && *(p + 1) == '\'') {
                    putchar(*p++); putchar(*p++);
                    continue;
                }
                putchar(*p++);
            }
            if (*p == '\'') putchar(*p++);
            printf(RST);
            continue;
        }
        /* Number */
        if (isdigit(*p) || (*p == '-' && isdigit(*(p + 1)))) {
            printf(MAG);
            if (*p == '-') putchar(*p++);
            while (*p && (isdigit(*p) || *p == '.')) putchar(*p++);
            printf(RST);
            continue;
        }
        /* Word (keyword or identifier) */
        if (isalpha(*p) || *p == '_') {
            const char *start = p;
            while (*p && (isalnum(*p) || *p == '_')) p++;
            int len = (int)(p - start);
            char word[128];
            if (len < (int)sizeof(word)) {
                memcpy(word, start, len);
                word[len] = '\0';
                if (is_sql_keyword(word))
                    printf(BYEL "%.*s" RST, len, start);
                else if (is_sql_type(word))
                    printf(CYN "%.*s" RST, len, start);
                else
                    printf("%.*s", len, start);
            } else {
                printf("%.*s", len, start);
            }
            continue;
        }
        /* Parenthesis, comma */
        if (*p == '(' || *p == ')' || *p == ',' || *p == ';') {
            printf(DIM "%c" RST, *p++);
            continue;
        }
        /* Star (SELECT *) */
        if (*p == '*') {
            printf(BOLD "%c" RST, *p++);
            continue;
        }
        /* Other */
        putchar(*p++);
    }
}

/* Print multi-line SQL with left margin and highlighting */
static void print_sql_display(const char *sql)
{
    printf("\n");
    const char *p = sql;
    while (*p) {
        printf("  " DIM "|" RST " ");
        const char *eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);
        /* Highlight this line */
        char *line = strndup(p, eol - p);
        highlight_print(line);
        free(line);
        printf("\n");
        p = (*eol) ? eol + 1 : eol;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   SQL Formatter (Single Line -> Multi Line)
   ═══════════════════════════════════════════════════════════════════════ */

/* Normalize whitespace */
static char *normalize_ws(const char *s)
{
    char *out = malloc(strlen(s) + 1);
    int oi = 0, prev_sp = 0;
    for (; *s; s++) {
        if (*s == ' ' || *s == '\t') {
            if (!prev_sp && oi > 0) out[oi++] = ' ';
            prev_sp = 1;
        } else {
            out[oi++] = *s;
            prev_sp = 0;
        }
    }
    while (oi > 0 && out[oi - 1] == ' ') oi--;
    out[oi] = '\0';
    return out;
}

/* Match keyword at word boundary */
static int match_kw_at(const char *s, const char *kw)
{
    int len = (int)strlen(kw);
    if (strncasecmp(s, kw, len) != 0) return 0;
    if (s[len] && (isalnum(s[len]) || s[len] == '_')) return 0;
    return len;
}

static char *format_create_table(const char *sql)
{
    char *result = malloc(strlen(sql) * 3 + 256);
    int ri = 0;
    const char *p = sql;

    /* Copy "CREATE TABLE name (" part */
    while (*p && *p != '(') result[ri++] = *p++;
    if (!*p) { result[ri] = '\0'; return result; }
    result[ri++] = '(';
    result[ri++] = '\n';
    p++; /* skip '(' */
    while (*p == ' ') p++;

    /* Split column definitions at commas */
    int depth = 0;
    while (*p) {
        if (*p == ')' && depth == 0) {
            result[ri++] = '\n';
            result[ri++] = ')';
            p++;
            break;
        }
        /* Indent */
        result[ri++] = ' ';
        result[ri++] = ' ';

        /* Copy until comma or closing paren at depth 0 */
        while (*p && !(*p == ',' && depth == 0) && !(*p == ')' && depth == 0)) {
            if (*p == '(') depth++;
            if (*p == ')') depth--;
            result[ri++] = *p++;
        }
        if (*p == ',') {
            result[ri++] = ',';
            result[ri++] = '\n';
            p++;
            while (*p == ' ') p++;
        }
    }
    /* Remaining characters */
    while (*p) result[ri++] = *p++;
    result[ri] = '\0';
    return result;
}

static char *format_dml(const char *sql)
{
    static const char *break_kws[] = {
        "FROM","WHERE","SET","VALUES","HAVING","LIMIT",
        "ORDER BY","GROUP BY",
        "LEFT JOIN","RIGHT JOIN","INNER JOIN","OUTER JOIN",
        "CROSS JOIN","FULL JOIN","JOIN",
        "UNION ALL","UNION",
        NULL
    };
    static const char *indent_kws[] = { "AND","OR", NULL };

    char *result = malloc(strlen(sql) * 2 + 256);
    int ri = 0;
    const char *p = sql;
    int in_string = 0;

    while (*p) {
        /* Skip inside strings */
        if (*p == '\'' && !in_string) {
            in_string = 1;
            result[ri++] = *p++;
            continue;
        }
        if (*p == '\'' && in_string) {
            in_string = 0;
            result[ri++] = *p++;
            continue;
        }
        if (in_string) {
            result[ri++] = *p++;
            continue;
        }

        int matched = 0;
        if (ri > 0) {
            for (int i = 0; break_kws[i]; i++) {
                int len = match_kw_at(p, break_kws[i]);
                if (len) {
                    result[ri++] = '\n';
                    result[ri++] = ' ';
                    result[ri++] = ' ';
                    memcpy(result + ri, p, len);
                    ri += len;
                    p += len;
                    matched = 1;
                    break;
                }
            }
            if (!matched) {
                for (int i = 0; indent_kws[i]; i++) {
                    int len = match_kw_at(p, indent_kws[i]);
                    if (len) {
                        result[ri++] = '\n';
                        result[ri++] = ' ';
                        result[ri++] = ' ';
                        result[ri++] = ' ';
                        result[ri++] = ' ';
                        memcpy(result + ri, p, len);
                        ri += len;
                        p += len;
                        matched = 1;
                        break;
                    }
                }
            }
        }
        if (!matched) result[ri++] = *p++;
    }
    result[ri] = '\0';
    return result;
}

static char *format_sql(const char *raw)
{
    char *norm = normalize_ws(raw);
    char *result;
    if (strncasecmp(norm, "CREATE TABLE", 12) == 0)
        result = format_create_table(norm);
    else
        result = format_dml(norm);
    free(norm);
    return result;
}

/* Flatten multi-line SQL to single line */
static char *flatten_sql(const char *sql)
{
    char *flat = strdup(sql);
    for (char *p = flat; *p; p++) {
        if (*p == '\n' || *p == '\r') *p = ' ';
    }
    return flat;
}

/* ═══════════════════════════════════════════════════════════════════════
   SQL Editor (Mini Editor)
   ═══════════════════════════════════════════════════════════════════════ */

typedef struct {
    char *buf;
    int   len;
    int   cap;
    int   cursor;
} Editor;

static void ed_ensure(Editor *ed, int need)
{
    if (ed->len + need >= ed->cap) {
        ed->cap = (ed->len + need + 256) * 2;
        ed->buf = realloc(ed->buf, ed->cap);
    }
}

static int ed_line_start(const char *buf, int cursor)
{
    int i = cursor;
    while (i > 0 && buf[i - 1] != '\n') i--;
    return i;
}

static int ed_line_end(const char *buf, int len, int cursor)
{
    int i = cursor;
    while (i < len && buf[i] != '\n') i++;
    return i;
}

static int ed_cur_row(const char *buf, int cursor)
{
    int row = 0;
    for (int i = 0; i < cursor; i++)
        if (buf[i] == '\n') row++;
    return row;
}

static int ed_cur_col(const char *buf, int cursor)
{
    return cursor - ed_line_start(buf, cursor);
}

static int ed_count_lines(const char *buf, int len)
{
    int n = 1;
    for (int i = 0; i < len; i++)
        if (buf[i] == '\n') n++;
    return n;
}

static void ed_render(Editor *ed)
{
    int rows, cols;
    get_term_size(&rows, &cols);

    /* Should already be in alternate screen */
    printf("\033[H\033[2J"); /* clear + home */

    /* Title */
    printf("  " BOLD "EvoSQL" RST " " DIM "%s" RST "\n", L->editor_title);
    printf("  " DIM);
    for (int i = 0; i < cols - 4 && i < 60; i++) putchar('-');
    printf(RST "\n\n");

    /* SQL lines */
    int line_no = 1;
    int pos = 0;
    int cursor_term_row = 0, cursor_term_col = 0;
    int crow = ed_cur_row(ed->buf, ed->cursor);
    /* Cursor column: UTF-8 display width */
    int ccol_display = 0;
    {
        int ls = ed_line_start(ed->buf, ed->cursor);
        for (int i = ls; i < ed->cursor; ) {
            unsigned char ch = (unsigned char)ed->buf[i];
            if (ch < 0x80) { i++; ccol_display++; }
            else if ((ch & 0xE0) == 0xC0) { i += 2; ccol_display++; }
            else if ((ch & 0xF0) == 0xE0) { i += 3; ccol_display++; }
            else if ((ch & 0xF8) == 0xF0) { i += 4; ccol_display++; }
            else { i++; ccol_display++; }
        }
    }

    while (pos <= ed->len) {
        int eol = ed_line_end(ed->buf, ed->len, pos);
        int line_len = eol - pos;

        /* Line number */
        printf("  " DIM "%3d " RST DIM "|" RST " ", line_no);

        /* Highlighted line */
        if (line_len > 0) {
            char *line = strndup(ed->buf + pos, line_len);
            highlight_print(line);
            free(line);
        }
        printf("\033[K\n"); /* Clear to end of line */

        /* Cursor position — header is 3 lines: title + dashes + blank */
        if (line_no - 1 == crow) {
            cursor_term_row = line_no + 3; /* +3: title(1) + dashes(1) + blank(1) */
            cursor_term_col = 8 + ccol_display; /* 8: "  NNN | " prefix */
        }

        line_no++;
        pos = eol + 1;
        if (eol == ed->len) break;
    }

    /* Footer */
    printf("\n  " DIM);
    for (int i = 0; i < cols - 4 && i < 60; i++) putchar('-');
    printf(RST "\n");
    printf("  " BCYN "Ctrl+S" RST " %s" DIM "  |  " RST
           BRED "Esc" RST " %s" DIM "  |  " RST
           BMAG "Ctrl+A" RST " %s\n", L->editor_save, L->editor_cancel, L->editor_ai);
    printf("  " DIM "↑↓←→" RST " %s" DIM "  |  " RST
           DIM "Enter" RST " %s" DIM "  |  " RST
           DIM "Tab" RST " %s\n", L->editor_move, L->editor_newline, L->editor_indent);

    /* Line/column info */
    printf("  " DIM);
    printf(L->editor_line_col, crow + 1, ccol_display + 1);
    printf(RST "\n");

    /* Position cursor */
    if (cursor_term_row > 0)
        printf("\033[%d;%dH", cursor_term_row, cursor_term_col);

    printf("\033[?25h"); /* Show cursor */
    fflush(stdout);
}

/*
 * AI-assisted SQL editing: send current SQL + natural language instruction
 * to the NL service. Returns new SQL (caller frees) or NULL on error.
 */
static char *ai_assist_sql(const char *current_sql, const char *instruction)
{
    /* Build a combined message: "Given this SQL:\n<sql>\n\nUser request: <instruction>" */
    size_t sql_len = current_sql ? strlen(current_sql) : 0;
    size_t inst_len = strlen(instruction);
    size_t msg_len = sql_len + inst_len + 128;
    char *msg = malloc(msg_len);

    if (sql_len > 0) {
        /* Flatten the SQL for single-line protocol */
        char *flat_sql = strdup(current_sql);
        for (char *p = flat_sql; *p; p++) {
            if (*p == '\n' || *p == '\r') *p = ' ';
        }
        snprintf(msg, msg_len, "Given this SQL: %s --- %s", flat_sql, instruction);
        free(flat_sql);
    } else {
        snprintf(msg, msg_len, "%s", instruction);
    }

    /* Send NL command */
    char cmd[65600];
    snprintf(cmd, sizeof(cmd), "NL %s", msg);
    free(msg);
    send_line(cmd);

    /* Read response */
    char line[65536];
    int rc = recv_line(line, sizeof(line), 120);
    if (rc < 0) return NULL;

    /* If we got SQL_PROPOSAL, extract the SQL */
    if (strncmp(line, "SQL_PROPOSAL ", 13) == 0) {
        return strdup(line + 13);
    }
    /* If we got a QUESTION, return it as-is (caller can display) */
    if (strncmp(line, "QUESTION ", 9) == 0) {
        return strdup(line + 9);
    }

    return NULL;
}

/*
 * Editor AI chat mode: temporarily exit alt screen, get user input,
 * call AI, replace editor buffer with result.
 * Returns 1 if buffer was updated, 0 if cancelled/no change.
 */
static int editor_ai_chat(Editor *ed)
{
    /* Temporarily exit alternate screen and raw mode */
    printf("\033[?1049l");
    disable_raw_mode();

    /* Show current SQL */
    printf("\n  " BCYN "AI" RST " " DIM "%s" RST "\n", L->editor_ai_title);
    if (ed->len > 0) {
        ed->buf[ed->len] = '\0';
        printf("  " DIM "%s" RST "\n", L->editor_ai_current);
        char *formatted = format_sql(ed->buf);
        print_sql_display(formatted);
        free(formatted);
    }
    printf("\n");

    /* Get instruction from user */
    char *input = readline("  " BCYN "AI" RST " " DIM ">" RST " ");
    if (!input || strlen(input) == 0) {
        free(input);
        /* Return to editor */
        enable_raw_mode();
        printf("\033[?1049h");
        return 0;
    }

    /* Show thinking indicator */
    printf("  " DIM "⠿ %s" RST, L->thinking);
    fflush(stdout);

    /* Call AI */
    ed->buf[ed->len] = '\0';
    char *result = ai_assist_sql(ed->buf, input);
    free(input);

    printf("\r\033[K");

    if (!result) {
        printf("  " BRED "\xe2\x9c\x97" RST " %s\n", L->editor_ai_no_resp);
        sleep(1);
        enable_raw_mode();
        printf("\033[?1049h");
        return 0;
    }

    /* Check if result looks like SQL (contains SQL keywords) or is a question */
    int is_sql = (strncasecmp(result, "SELECT", 6) == 0 ||
                  strncasecmp(result, "CREATE", 6) == 0 ||
                  strncasecmp(result, "INSERT", 6) == 0 ||
                  strncasecmp(result, "UPDATE", 6) == 0 ||
                  strncasecmp(result, "DELETE", 6) == 0 ||
                  strncasecmp(result, "ALTER", 5) == 0 ||
                  strncasecmp(result, "DROP", 4) == 0);

    if (!is_sql) {
        /* AI asked a question or returned non-SQL */
        printf("  " MAG "?" RST " %s\n", result);
        free(result);
        printf("  " DIM "%s" RST "\n", L->editor_ai_back);
        getchar();
        enable_raw_mode();
        printf("\033[?1049h");
        return 0;
    }

    /* Replace editor buffer with new SQL */
    int new_len = (int)strlen(result);
    if (new_len + 1 > ed->cap) {
        ed->cap = new_len + 1024;
        ed->buf = realloc(ed->buf, ed->cap);
    }
    memcpy(ed->buf, result, new_len);
    ed->buf[new_len] = '\0';
    ed->len = new_len;
    ed->cursor = new_len;
    free(result);

    /* Also update pending SQL on server */
    char *flat = flatten_sql(ed->buf);
    send_line("REJECT");
    char tmp[256]; recv_line(tmp, sizeof(tmp), 5);
    char scmd[65600];
    snprintf(scmd, sizeof(scmd), "SQL %s", flat);
    send_line(scmd);
    recv_line(tmp, sizeof(tmp), 10); /* SQL_PROPOSAL echo */
    free(flat);

    /* Return to editor */
    enable_raw_mode();
    printf("\033[?1049h");
    return 1;
}

/* Run editor, return edited SQL (NULL = cancelled) */
static char *editor_run(const char *initial_sql)
{
    Editor ed;
    ed.len = (int)strlen(initial_sql);
    ed.cap = ed.len + 1024;
    ed.buf = malloc(ed.cap);
    memcpy(ed.buf, initial_sql, ed.len);
    ed.buf[ed.len] = '\0';
    ed.cursor = ed.len; /* Position at end */

    enable_raw_mode();
    printf("\033[?1049h"); /* Switch to alternate screen */

    ed_render(&ed);

    while (1) {
        int key = read_key();
        if (key < 0) break;

        /* Ctrl+S: save */
        if (key == K_CTRL_S) {
            printf("\033[?1049l");
            disable_raw_mode();
            ed.buf[ed.len] = '\0';
            return ed.buf;
        }
        /* Esc or Ctrl+C: cancel */
        if (key == K_ESCAPE || key == K_CTRL_C) {
            printf("\033[?1049l");
            disable_raw_mode();
            free(ed.buf);
            return NULL;
        }
        /* Ctrl+A: AI chat */
        if (key == K_CTRL_A) {
            ed.buf[ed.len] = '\0';
            editor_ai_chat(&ed);
            ed_render(&ed);
            continue;
        }

        /* Arrow keys (UTF-8 aware) */
        if (key == K_LEFT) {
            if (ed.cursor > 0) {
                ed.cursor--;
                /* Skip UTF-8 continuation bytes */
                while (ed.cursor > 0 &&
                       (ed.buf[ed.cursor] & 0xC0) == 0x80)
                    ed.cursor--;
            }
        }
        else if (key == K_RIGHT) {
            if (ed.cursor < ed.len) {
                ed.cursor++;
                /* Skip UTF-8 continuation bytes */
                while (ed.cursor < ed.len &&
                       (ed.buf[ed.cursor] & 0xC0) == 0x80)
                    ed.cursor++;
            }
        }
        else if (key == K_UP) {
            int ls = ed_line_start(ed.buf, ed.cursor);
            if (ls > 0) {
                int col = ed.cursor - ls;
                int prev_ls = ed_line_start(ed.buf, ls - 1);
                int prev_len = (ls - 1) - prev_ls;
                ed.cursor = prev_ls + (col < prev_len ? col : prev_len);
            }
        }
        else if (key == K_DOWN) {
            int le = ed_line_end(ed.buf, ed.len, ed.cursor);
            if (le < ed.len) {
                int ls = ed_line_start(ed.buf, ed.cursor);
                int col = ed.cursor - ls;
                int next_ls = le + 1;
                int next_le = ed_line_end(ed.buf, ed.len, next_ls);
                int next_len = next_le - next_ls;
                ed.cursor = next_ls + (col < next_len ? col : next_len);
            }
        }
        else if (key == K_HOME) {
            ed.cursor = ed_line_start(ed.buf, ed.cursor);
        }
        else if (key == K_END) {
            ed.cursor = ed_line_end(ed.buf, ed.len, ed.cursor);
        }
        /* Delete (UTF-8 aware) */
        else if (key == K_BACKSPACE) {
            if (ed.cursor > 0) {
                /* UTF-8: also delete continuation bytes (10xxxxxx) */
                int del = 1;
                while (ed.cursor - del > 0 &&
                       (ed.buf[ed.cursor - del] & 0xC0) == 0x80)
                    del++;
                memmove(ed.buf + ed.cursor - del, ed.buf + ed.cursor,
                        ed.len - ed.cursor + 1);
                ed.cursor -= del;
                ed.len -= del;
            }
        }
        else if (key == K_DELETE) {
            if (ed.cursor < ed.len) {
                /* UTF-8: delete all bytes */
                int del = 1;
                while (ed.cursor + del < ed.len &&
                       (ed.buf[ed.cursor + del] & 0xC0) == 0x80)
                    del++;
                memmove(ed.buf + ed.cursor, ed.buf + ed.cursor + del,
                        ed.len - ed.cursor - del + 1);
                ed.len -= del;
            }
        }
        /* Enter: new line */
        else if (key == K_ENTER || key == 10) {
            ed_ensure(&ed, 1);
            memmove(ed.buf + ed.cursor + 1, ed.buf + ed.cursor,
                    ed.len - ed.cursor + 1);
            ed.buf[ed.cursor] = '\n';
            ed.cursor++;
            ed.len++;
        }
        /* Tab: 2 spaces */
        else if (key == K_TAB) {
            ed_ensure(&ed, 2);
            memmove(ed.buf + ed.cursor + 2, ed.buf + ed.cursor,
                    ed.len - ed.cursor + 1);
            ed.buf[ed.cursor] = ' ';
            ed.buf[ed.cursor + 1] = ' ';
            ed.cursor += 2;
            ed.len += 2;
        }
        /* UTF-8 multi-byte character */
        else if (key == K_UTF8) {
            int n = g_utf8_len;
            ed_ensure(&ed, n);
            memmove(ed.buf + ed.cursor + n, ed.buf + ed.cursor,
                    ed.len - ed.cursor + 1);
            memcpy(ed.buf + ed.cursor, g_utf8_buf, n);
            ed.cursor += n;
            ed.len += n;
        }
        /* Printable ASCII character */
        else if (key >= 32 && key < 127) {
            ed_ensure(&ed, 1);
            memmove(ed.buf + ed.cursor + 1, ed.buf + ed.cursor,
                    ed.len - ed.cursor + 1);
            ed.buf[ed.cursor] = (char)key;
            ed.cursor++;
            ed.len++;
        }

        ed_render(&ed);
    }

    printf("\033[?1049l");
    disable_raw_mode();
    free(ed.buf);
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════
   UI Components
   ═══════════════════════════════════════════════════════════════════════ */

static void print_banner(void)
{
    printf("\n"
        "  " BCYN "EvoSQL" RST " " DIM "Smart Console" RST "\n"
        "  " DIM "%s" RST "\n\n", L->app_subtitle);
}

static void print_help(void)
{
    printf("\n"
        "  " BYEL "%s" RST "\n", L->cmd_title);
    printf(
        "    " BOLD "%s" RST "        SQL\n", L->cmd_natural);
    printf(
        "    " BCYN "/schema" RST "                  %s\n", L->cmd_schema);
    printf(
        "    " BCYN "/db <name>" RST "               %s\n", L->cmd_db);
    printf(
        "    " BCYN "/sql <SQL>" RST "               %s\n", L->cmd_sql);
    printf(
        "    " BCYN "/tables" RST "                  %s\n", L->cmd_tables);
    printf(
        "    " BCYN "/schemas" RST "                 %s\n", L->cmd_schemas);
    printf(
        "    " BCYN "/databases" RST "               %s\n", L->cmd_databases);
    printf(
        "    " BCYN "/users" RST "                   %s\n", L->cmd_users);
    printf(
        "    " BCYN "/grants" RST "                  %s\n", L->cmd_grants);
    printf(
        "    " BCYN "/describe <table>" RST "        %s\n", L->cmd_describe);
    printf(
        "    " BCYN "/ai-model" RST "                %s\n", L->cmd_ai_model);
    printf(
        "    " BCYN "/help" RST "                    %s\n", L->cmd_help);
    printf(
        "    " BCYN "/quit" RST "                    %s\n", L->cmd_quit);
    printf("\n"
        "  " BYEL "%s" RST "\n", L->after_sql_title);
    printf(
        "    " BGRN "Enter" RST "                    %s\n", L->confirm_execute);
    printf(
        "    " BCYN "Ctrl+E" RST " / " BCYN "Ctrl+O" RST "        %s\n", L->confirm_edit);
    printf(
        "    " BRED "Esc" RST "                      %s\n", L->confirm_cancel);
    printf("\n"
        "  " BYEL "%s:" RST "\n", L->editor_title);
    printf(
        "    " DIM "↑↓←→" RST "                    %s\n", L->editor_move);
    printf(
        "    " DIM "Ctrl+S" RST "                   %s\n", L->editor_save);
    printf(
        "    " BMAG "Ctrl+A" RST "                   %s\n", L->editor_ai);
    printf(
        "    " DIM "Esc" RST "                      %s\n", L->editor_cancel);
    printf("\n");
    printf(
        "    " DIM "%s" RST "\n", L->example1);
    printf(
        "    " DIM "%s" RST "\n", L->example2);
    printf(
        "    " DIM "%s" RST "\n", L->example3);
    printf(
        "    " DIM "%s" RST "\n\n", L->example4);
}

static void print_result(const char *text)
{
    printf("\n  " BGRN "+" RST " ");
    const char *p = text;
    while (*p) {
        if (*p == '|') {
            printf(DIM " | " RST);
            p++;
            if (*p == ' ') p++;
        } else if (*p == '=') {
            printf(DIM "=" RST);
            p++;
        } else {
            putchar(*p++);
        }
    }
    printf("\n");
}

static void print_error(const char *msg)
{
    printf("\n  " BRED "x" RST " %s\n", msg);
}

static void print_info(const char *msg)
{
    printf("  " BCYN "i" RST " %s\n", msg);
}

static void print_question(const char *text)
{
    printf("\n  " MAG "?" RST " %s\n", text);
}

/* ═══════════════════════════════════════════════════════════════════════
   Provider Menu & Config
   ═══════════════════════════════════════════════════════════════════════ */

/*
 * Interactive arrow-key menu. Returns 0-based index of selected item.
 * items[] is an array of labels, n_items is the count.
 */
static int arrow_menu(const char *title, const char *items[], int n_items, int initial)
{
    int sel = initial;
    if (sel < 0 || sel >= n_items) sel = 0;

    /* total lines drawn: title(1) + blank(1) + items(n_items) + blank(1) + footer(1) */
    int total_lines = n_items + 4;

    enable_raw_mode();
    printf("\033[?25l"); /* hide cursor */
    fflush(stdout);

    int first = 1;
    for (;;) {
        /* Move cursor back to top of menu (skip on first draw) */
        if (!first)
            printf("\033[%dA\r", total_lines - 1);
        first = 0;

        /* Title */
        printf("\033[K  " BYEL "%s" RST "\n", title);
        printf("\033[K\n");

        /* Items */
        for (int i = 0; i < n_items; i++) {
            printf("\033[K");  /* clear line */
            if (i == sel)
                printf("  " BGRN "\xe2\x96\xb6" RST " " BOLD "%s" RST "\n", items[i]);
            else
                printf("    " DIM "%s" RST "\n", items[i]);
        }

        printf("\033[K\n");
        printf("\033[K  " DIM "\xe2\x86\x91\xe2\x86\x93 select   Enter confirm" RST);
        fflush(stdout);

        int key = read_key();

        if (key == K_UP) {
            sel = (sel > 0) ? sel - 1 : n_items - 1;
        } else if (key == K_DOWN) {
            sel = (sel < n_items - 1) ? sel + 1 : 0;
        } else if (key == K_ENTER || key == '\r') {
            break;
        } else if (key == K_ESCAPE || key == K_CTRL_C) {
            sel = 0;
            break;
        } else if (key >= '1' && key <= '0' + n_items) {
            sel = key - '1';
            break;
        }
    }

    /* Show cursor, move past the menu */
    printf("\033[?25h");
    printf("\n\n");
    fflush(stdout);

    disable_raw_mode();
    return sel;
}

static void provider_menu(ClientProviderConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    const char *items[] = {
        L->provider_ollama,
        L->provider_groq,
        L->provider_gemini,
        L->provider_claude,
        L->provider_openai,
        L->provider_custom,
    };
    int n_items = 6;

    printf("\n");
    int choice = arrow_menu(L->provider_title, items, n_items, 0);

    char *tmp;

    switch (choice) {
    case 0: /* Ollama (local) */
        cfg->type = 0; /* OLLAMA */
        strncpy(cfg->model, "qwen2.5:7b", sizeof(cfg->model) - 1);
        strncpy(cfg->base_url, "http://localhost:11434", sizeof(cfg->base_url) - 1);

        /* Ask model */
        {
            printf("  " DIM);
            printf(L->model_prompt, "qwen2.5:7b");
            printf(RST " ");
            fflush(stdout);
            tmp = readline("");
            if (tmp && *tmp) strncpy(cfg->model, tmp, sizeof(cfg->model) - 1);
            free(tmp);
        }
        break;

    case 1: /* Groq */
        cfg->type = 2; /* OPENAI */
        strncpy(cfg->model, "llama-3.3-70b-versatile", sizeof(cfg->model) - 1);
        strncpy(cfg->base_url, "https://api.groq.com/openai", sizeof(cfg->base_url) - 1);

        printf("  " BYEL "%s" RST " ", L->api_key_prompt);
        fflush(stdout);
        tmp = readline("");
        if (tmp && *tmp) strncpy(cfg->api_key, tmp, sizeof(cfg->api_key) - 1);
        free(tmp);
        break;

    case 2: /* Gemini */
        cfg->type = 2; /* OPENAI (compatible) */
        strncpy(cfg->model, "gemini-2.5-flash", sizeof(cfg->model) - 1);
        strncpy(cfg->base_url, "https://generativelanguage.googleapis.com/v1beta/openai",
                sizeof(cfg->base_url) - 1);

        printf("  " BYEL "%s" RST " ", L->api_key_prompt);
        fflush(stdout);
        tmp = readline("");
        if (tmp && *tmp) strncpy(cfg->api_key, tmp, sizeof(cfg->api_key) - 1);
        free(tmp);
        break;

    case 3: /* Claude */
        cfg->type = 1; /* CLAUDE */
        strncpy(cfg->model, "claude-sonnet-4-20250514", sizeof(cfg->model) - 1);

        printf("  " BYEL "%s" RST " ", L->api_key_prompt);
        fflush(stdout);
        tmp = readline("");
        if (tmp && *tmp) strncpy(cfg->api_key, tmp, sizeof(cfg->api_key) - 1);
        free(tmp);
        break;

    case 4: /* OpenAI */
        cfg->type = 2; /* OPENAI */
        strncpy(cfg->model, "gpt-4o", sizeof(cfg->model) - 1);
        strncpy(cfg->base_url, "https://api.openai.com", sizeof(cfg->base_url) - 1);

        printf("  " BYEL "%s" RST " ", L->api_key_prompt);
        fflush(stdout);
        tmp = readline("");
        if (tmp && *tmp) strncpy(cfg->api_key, tmp, sizeof(cfg->api_key) - 1);
        free(tmp);
        break;

    case 5: /* Custom endpoint */
        cfg->type = 2; /* OPENAI (compatible) */

        printf("  " BYEL "%s" RST " ", L->api_key_prompt);
        fflush(stdout);
        tmp = readline("");
        if (tmp && *tmp) strncpy(cfg->api_key, tmp, sizeof(cfg->api_key) - 1);
        free(tmp);

        {
            printf("  " DIM);
            printf(L->model_prompt, "default");
            printf(RST " ");
            fflush(stdout);
            tmp = readline("");
            if (tmp && *tmp)
                strncpy(cfg->model, tmp, sizeof(cfg->model) - 1);
            else
                strncpy(cfg->model, "default", sizeof(cfg->model) - 1);
            free(tmp);
        }

        {
            printf("  " DIM);
            printf(L->url_prompt, "http://localhost:8080");
            printf(RST " ");
            fflush(stdout);
            tmp = readline("");
            if (tmp && *tmp)
                strncpy(cfg->base_url, tmp, sizeof(cfg->base_url) - 1);
            else
                strncpy(cfg->base_url, "http://localhost:8080", sizeof(cfg->base_url) - 1);
            free(tmp);
        }
        break;
    }

    printf("\n");
    print_info(L->provider_saved);
    printf("\n");
}

/* Format and send PROVIDER command to the NL service */
static void send_provider_cmd(ClientProviderConfig *cfg)
{
    char cmd[2048];

    switch (cfg->type) {
    case 0: /* OLLAMA */
        /* Don't send base_url for Ollama — let the server use its own
           OLLAMA_HOST env (important when client and server run in
           different network contexts, e.g. Docker) */
        snprintf(cmd, sizeof(cmd), "PROVIDER OLLAMA %s - -",
                 cfg->model);
        break;
    case 1: /* CLAUDE */
        snprintf(cmd, sizeof(cmd), "PROVIDER CLAUDE %s %s",
                 cfg->model, cfg->api_key);
        break;
    case 2: /* OPENAI */
        snprintf(cmd, sizeof(cmd), "PROVIDER OPENAI %s %s %s",
                 cfg->model, cfg->api_key, cfg->base_url);
        break;
    default:
        snprintf(cmd, sizeof(cmd), "PROVIDER OLLAMA %s - -",
                 cfg->model);
        break;
    }

    send_line(cmd);
    char tmp[256];
    recv_line(tmp, sizeof(tmp), 10);
}

/* ═══════════════════════════════════════════════════════════════════════
   Direct SQL Execution (for slash commands — no confirm dialog)
   ═══════════════════════════════════════════════════════════════════════ */

static void exec_direct_sql(const char *sql)
{
    char cmd[65600];
    snprintf(cmd, sizeof(cmd), "SQL %s", sql);
    send_line(cmd);

    char line[65536];
    if (recv_line(line, sizeof(line), 15) < 0) {
        print_error(L->timeout);
        return;
    }

    if (strncmp(line, "SQL_PROPOSAL ", 13) == 0) {
        /* Auto-execute: send EXECUTE immediately */
        send_line("EXECUTE");
        char result[65536];
        if (recv_line(result, sizeof(result), 30) >= 0) {
            if (strncmp(result, "RESULT ", 7) == 0)
                print_result(result + 7);
            else if (strncmp(result, "ERROR", 5) == 0)
                print_error(result + 6);
            else
                print_info(result);
        } else {
            print_error(L->timeout);
        }
    } else if (strncmp(line, "ERROR", 5) == 0) {
        print_error(line + 6);
    } else {
        print_info(line);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   Connection
   ═══════════════════════════════════════════════════════════════════════ */

static int nl_connect(void)
{
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_port);

    if (inet_pton(AF_INET, g_host, &addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(g_host);
        if (!he) { close(g_sock); g_sock = -1; return -1; }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (connect(g_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(g_sock); g_sock = -1;
        return -1;
    }

    char line[4096];
    if (recv_line(line, sizeof(line), 10) < 0 || strcmp(line, "NL_READY") != 0) {
        close(g_sock); g_sock = -1;
        return -1;
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
   Confirm & Execute Flow
   ═══════════════════════════════════════════════════════════════════════ */

static void execute_pending(void)
{
    printf("\r\033[K  " DIM "%s" RST, L->executing);
    fflush(stdout);
    send_line("EXECUTE");
    char line[65536];
    if (recv_line(line, sizeof(line), 30) >= 0) {
        printf("\r\033[K");
        if (strncmp(line, "RESULT ", 7) == 0)
            print_result(line + 7);
        else if (strncmp(line, "ERROR", 5) == 0)
            print_error(line + 6);
        else
            print_info(line);
    } else {
        printf("\r\033[K");
        print_error(L->timeout);
    }
}

static void do_confirm(const char *raw_sql)
{
    char *formatted = format_sql(raw_sql);
    char *current_raw = strdup(raw_sql);

    while (1) {
        /* Show SQL */
        print_sql_display(formatted);
        printf("\n  " BGRN "Enter" RST " %s" DIM "  |  " RST
               BCYN "Ctrl+E" RST " %s" DIM "  |  " RST
               BRED "Esc" RST " %s\n\n",
               L->confirm_execute, L->confirm_edit, L->confirm_cancel);

        enable_raw_mode();
        int key = read_key();
        disable_raw_mode();

        /* Enter: execute */
        if (key == K_ENTER || key == 10) {
            execute_pending();
            break;
        }
        /* Esc: cancel */
        if (key == K_ESCAPE) {
            send_line("REJECT");
            char tmp[256]; recv_line(tmp, sizeof(tmp), 5);
            print_info(L->cancelled);
            break;
        }
        /* Ctrl+E or Ctrl+O: edit */
        if (key == K_CTRL_E || key == K_CTRL_O) {
            char *edited = editor_run(formatted);
            if (edited) {
                free(formatted);
                free(current_raw);
                formatted = edited;
                current_raw = flatten_sql(edited);

                /* Update service state */
                send_line("REJECT");
                char tmp[256]; recv_line(tmp, sizeof(tmp), 5);
                char cmd[65600];
                snprintf(cmd, sizeof(cmd), "SQL %s", current_raw);
                send_line(cmd);
                char line[65536];
                recv_line(line, sizeof(line), 10); /* SQL_PROPOSAL echo */
                /* Return to confirm screen */
                continue;
            }
            /* Edit cancelled — return to confirm screen */
            continue;
        }
    }

    free(formatted);
    free(current_raw);
}

/* ═══════════════════════════════════════════════════════════════════════
   Tab→AI completion for /sql mode
   ═══════════════════════════════════════════════════════════════════════ */

static char *sql_ai_generator(const char *text, int state)
{
    (void)text; (void)state;
    return NULL; /* no default completions */
}

static char **sql_ai_completion(const char *text, int start, int end)
{
    (void)text; (void)start; (void)end;

    /* Only trigger when line starts with "/sql " */
    if (strncasecmp(rl_line_buffer, "/sql ", 5) != 0)
        return NULL;

    /* Need some SQL text after "/sql " */
    const char *partial = rl_line_buffer + 5;
    while (*partial == ' ') partial++;
    if (*partial == '\0') return NULL;

    /* Suppress default filename completion */
    rl_attempted_completion_over = 1;

    /* Send partial SQL to AI for completion */
    printf("\n  " DIM "\xe2\xa0\xbf %s" RST, L->thinking);
    fflush(stdout);

    char *result = ai_assist_sql(partial, "Complete or fix this SQL");

    printf("\r\033[K");

    if (!result) return NULL;

    /* Check if it looks like SQL */
    int is_sql = (strncasecmp(result, "SELECT", 6) == 0 ||
                  strncasecmp(result, "CREATE", 6) == 0 ||
                  strncasecmp(result, "INSERT", 6) == 0 ||
                  strncasecmp(result, "UPDATE", 6) == 0 ||
                  strncasecmp(result, "DELETE", 6) == 0 ||
                  strncasecmp(result, "ALTER", 5) == 0 ||
                  strncasecmp(result, "DROP", 4) == 0);

    if (!is_sql) {
        /* AI asked a question — show it */
        printf("\n  " MAG "?" RST " %s\n", result);
        free(result);
        rl_on_new_line();
        return NULL;
    }

    /* Replace line buffer with /sql + AI result */
    char *new_line = malloc(strlen(result) + 6);
    sprintf(new_line, "/sql %s", result);
    free(result);

    int new_len = (int)strlen(new_line);
    if (new_len < rl_end) {
        /* Shrink */
        memcpy(rl_line_buffer, new_line, new_len);
        rl_line_buffer[new_len] = '\0';
    } else {
        /* Grow — copy what fits */
        strncpy(rl_line_buffer, new_line, rl_end);
        rl_line_buffer[rl_end] = '\0';
        new_len = rl_end;
    }
    rl_point = new_len;
    rl_end = new_len;
    free(new_line);

    rl_on_new_line();
    rl_redisplay();

    /* Return empty completion list (we already replaced the line) */
    char **matches = malloc(2 * sizeof(char *));
    matches[0] = strdup("");
    matches[1] = NULL;
    return matches;
}

/* ═══════════════════════════════════════════════════════════════════════
   Startup Checks
   ═══════════════════════════════════════════════════════════════════════ */

/*
 * Run a command and capture first line of output.
 * Returns 0 on success, -1 on failure. Output is trimmed.
 */
static int run_capture(const char *cmd, char *out, size_t out_sz)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    out[0] = '\0';
    if (fgets(out, (int)out_sz, fp) != NULL) {
        /* trim trailing whitespace */
        size_t len = strlen(out);
        while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r' || out[len-1] == ' '))
            out[--len] = '\0';
    }
    return pclose(fp) == 0 ? 0 : -1;
}

/*
 * Simple HTTP GET via raw socket (no curl/libcurl dependency).
 * Returns heap-allocated response body, or NULL on error.
 */
static char *http_get_simple(const char *url)
{
    /* Parse URL: http://host:port/path */
    if (strncmp(url, "http://", 7) != 0) return NULL;
    const char *hoststart = url + 7;

    char host[256] = {0};
    int port = 80;
    char path[512] = "/";

    const char *colon = strchr(hoststart, ':');
    const char *slash = strchr(hoststart, '/');

    if (colon && (!slash || colon < slash)) {
        int hlen = (int)(colon - hoststart);
        if (hlen > 255) hlen = 255;
        strncpy(host, hoststart, hlen);
        port = atoi(colon + 1);
        if (slash) strncpy(path, slash, sizeof(path) - 1);
    } else if (slash) {
        int hlen = (int)(slash - hoststart);
        if (hlen > 255) hlen = 255;
        strncpy(host, hoststart, hlen);
        strncpy(path, slash, sizeof(path) - 1);
    } else {
        strncpy(host, hoststart, sizeof(host) - 1);
    }

    /* Resolve and connect */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) return NULL;

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { freeaddrinfo(res); return NULL; }

    /* 3 second timeout */
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        close(sock);
        freeaddrinfo(res);
        return NULL;
    }
    freeaddrinfo(res);

    /* Send GET request */
    char req[1024];
    snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
        path, host, port);
    send(sock, req, strlen(req), 0);

    /* Read response */
    char *buf = malloc(65536);
    int total = 0;
    while (total < 65535) {
        int n = (int)recv(sock, buf + total, 65535 - total, 0);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';
    close(sock);

    if (total == 0) { free(buf); return NULL; }

    /* Find body (after \r\n\r\n) */
    char *body = strstr(buf, "\r\n\r\n");
    if (!body) { free(buf); return NULL; }
    body += 4;

    char *result = strdup(body);
    free(buf);
    return result;
}

/*
 * Parse model names from Ollama /api/tags JSON response.
 * Returns number of models found. Names stored in models[] (caller frees each).
 */
static int parse_ollama_models(const char *json, char *models[], int max_models)
{
    int count = 0;
    const char *p = json;

    while (count < max_models && (p = strstr(p, "\"name\"")) != NULL) {
        p += 6; /* skip "name" */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p != ':') { p++; continue; }
        p++; /* skip ':' */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p != '"') { p++; continue; }
        p++; /* skip opening quote */

        const char *start = p;
        while (*p && *p != '"') p++;
        if (*p != '"') break;

        models[count] = strndup(start, (size_t)(p - start));
        count++;
        p++;
    }

    return count;
}

/*
 * Check Ollama: connect to API, list models or show docker run hint.
 * Returns: 0 = ready, -1 = not available
 */
static int check_ollama(void)
{
    /* Determine Ollama URL: OLLAMA_HOST env, or default */
    const char *ollama_env = getenv("OLLAMA_HOST");
    char ollama_url[512];

    if (ollama_env && ollama_env[0]) {
        /* If no http:// prefix, add it */
        if (strncmp(ollama_env, "http://", 7) == 0 ||
            strncmp(ollama_env, "https://", 8) == 0)
            strncpy(ollama_url, ollama_env, sizeof(ollama_url) - 1);
        else
            snprintf(ollama_url, sizeof(ollama_url), "http://%s", ollama_env);
    } else {
        strncpy(ollama_url, "http://localhost:11434", sizeof(ollama_url) - 1);
    }

    /* Strip trailing slash */
    int ulen = (int)strlen(ollama_url);
    if (ulen > 0 && ollama_url[ulen - 1] == '/') ollama_url[ulen - 1] = '\0';

    printf("  " DIM "%s" RST, L->ollama_checking);
    fflush(stdout);

    /* Try to reach Ollama API */
    char api_url[1024];
    snprintf(api_url, sizeof(api_url), "%s/api/tags", ollama_url);

    char *response = http_get_simple(api_url);

    if (!response) {
        /* Ollama not reachable */
        printf("\r\033[K  " BYEL "!" RST " %s\n", L->ollama_not_found);
        printf("  " DIM "%s" RST "\n", L->ollama_run_docker);
        printf("\n    " BCYN "docker run -d -p 11434:11434 --name ollama ollama/ollama" RST "\n\n");
        printf("  " DIM "%s" RST "\n\n", L->ollama_or_manual);
        return -1;
    }

    printf("\r\033[K  " BGRN "+" RST " %s " DIM "(%s)" RST "\n", L->ollama_connected, ollama_url);

    /* Parse models */
    char *models[64];
    int n_models = parse_ollama_models(response, models, 64);
    free(response);

    if (n_models == 0) {
        /* Ollama running but no models */
        printf("  " BYEL "!" RST " %s\n", L->ollama_no_models);
        printf("  " DIM "%s" RST "\n", L->ollama_pull_hint);
        printf("\n    " BCYN "ollama pull qwen2.5:7b" RST "\n\n");
        return 0;
    }

    /* Show available models */
    printf("  " DIM "%s" RST "\n", L->ollama_models_title);
    for (int i = 0; i < n_models; i++) {
        printf("    " BCYN "\xe2\x80\xa2" RST " %s\n", models[i]);
        free(models[i]);
    }

    return 0;
}

/*
 * Check if EvoSQL is reachable.
 * Uses EVOSQL_HOST / EVOSQL_PORT env vars (same as nl-service),
 * so it works both on the host and inside Docker.
 */
static void check_evosql(void)
{
    const char *evo_host = getenv("EVOSQL_HOST");
    if (!evo_host || !evo_host[0]) evo_host = "127.0.0.1";

    const char *evo_port_str = getenv("EVOSQL_PORT");
    int evo_port = (evo_port_str && evo_port_str[0]) ? atoi(evo_port_str) : 9967;

    printf("  " DIM "%s" RST, L->evosql_checking);
    fflush(stdout);

    /* Resolve host (supports both IP and hostname like "evosql") */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", evo_port);

    if (getaddrinfo(evo_host, port_str, &hints, &res) != 0) {
        printf("\r\033[K  " BYEL "!" RST " %s\n", L->evosql_not_found);
        printf("  " DIM "%s" RST "\n", L->evosql_docker_hint);
        printf("\n    " BCYN "docker run -d -p 5433:5433 -p 9967:9967 \\\n"
               "      -e EVOSQL_USER_NAME=admin -e EVOSQL_PASSWORD=admin \\\n"
               "      evolutiondb/evosql" RST "\n\n");
        return;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        printf("\r\033[K  " BYEL "!" RST " %s\n", L->evosql_not_found);
        return;
    }

    /* 2s connect timeout */
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int rc = connect(sock, res->ai_addr, res->ai_addrlen);
    close(sock);
    freeaddrinfo(res);

    if (rc == 0) {
        printf("\r\033[K  " BGRN "+" RST " %s " DIM "(%s:%d)" RST "\n",
               L->evosql_found, evo_host, evo_port);
    } else {
        printf("\r\033[K  " BYEL "!" RST " %s\n", L->evosql_not_found);
        printf("  " DIM "%s" RST "\n", L->evosql_docker_hint);
        printf("\n    " BCYN "docker run -d -p 5433:5433 -p 9967:9967 \\\n"
               "      -e EVOSQL_USER_NAME=admin -e EVOSQL_PASSWORD=admin \\\n"
               "      evolutiondb/evosql" RST "\n\n");
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   Main Program
   ═══════════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv)
{
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc)
            strncpy(g_host, argv[++i], sizeof(g_host) - 1);
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            g_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc)
            strncpy(g_db, argv[++i], sizeof(g_db) - 1);
        else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: nl-client [--host HOST] [--port PORT] [--db DB]\n");
            return 0;
        }
    }

    signal(SIGPIPE, SIG_IGN);

    /* i18n */
    L = i18n_get();

    /* Readline history */
    snprintf(g_histfile, sizeof(g_histfile), "%s/.evosql_nl_history",
             getenv("HOME") ? getenv("HOME") : "/tmp");
    read_history(g_histfile);

    print_banner();
    printf("\n");

    /* Startup checks */
    check_ollama();
    check_evosql();
    printf("\n");

    /* Provider config: try loading saved config, else show menu */
    if (config_load(&g_provider) != 0) {
        provider_menu(&g_provider);
        config_save(&g_provider);
    }

    /* Connect — auto-start nl-service if not running */
    {
        char msg[512];
        snprintf(msg, sizeof(msg), L->connecting, g_host, g_port);
        printf("  " DIM "%s" RST, msg);
        fflush(stdout);
    }

    if (nl_connect() < 0) {
        printf("\r\033[K  " DIM "%s" RST, L->service_starting);
        fflush(stdout);

        /* Find nl-service binary next to our own executable */
        char svc_path[1024] = "nl-service";
        {
            /* Try argv[0] directory first */
            const char *slash = strrchr(argv[0], '/');
            if (slash) {
                int dir_len = (int)(slash - argv[0]);
                snprintf(svc_path, sizeof(svc_path), "%.*s/nl-service", dir_len, argv[0]);
            }
        }

        pid_t pid = fork();
        if (pid == 0) {
            /* Child: redirect stdout/stderr to /dev/null, exec nl-service */
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) { dup2(devnull, STDOUT_FILENO); dup2(devnull, STDERR_FILENO); close(devnull); }
            execlp(svc_path, "nl-service", NULL);
            /* If svc_path failed, try PATH */
            execlp("nl-service", "nl-service", NULL);
            _exit(127);
        } else if (pid > 0) {
            g_service_pid = pid;

            /* Wait up to 5 seconds for service to accept connections */
            int connected = 0;
            for (int i = 0; i < 20; i++) {
                usleep(250000);
                if (nl_connect() == 0) { connected = 1; break; }
            }

            if (!connected) {
                printf("\r\033[K  " BRED "x" RST " %s\n\n", L->service_start_fail);
                kill(pid, SIGTERM);
                waitpid(pid, NULL, 0);
                g_service_pid = 0;
                return 1;
            }
            printf("\r\033[K  " BGRN "+" RST " %s " DIM "(%s:%d)" RST "\n", L->service_ready, g_host, g_port);
        } else {
            printf("\r\033[K  " BRED "x" RST " %s\n\n", L->service_start_fail);
            return 1;
        }
    } else {
        printf("\r\033[K  " BGRN "+" RST " %s " DIM "(%s:%d)" RST "\n",
               L->connected, g_host, g_port);
    }

    /* Send provider config */
    send_provider_cmd(&g_provider);

    /* Set database */
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "DATABASE %s", g_db);
        send_line(cmd);
        char tmp[256];
        recv_line(tmp, sizeof(tmp), 10);
    }
    printf("  " DIM "%s" RST " " BCYN "%s" RST "\n", L->database_label, g_db);

    print_help();

    /* Set up Tab→AI completion for /sql mode */
    rl_attempted_completion_function = sql_ai_completion;

    /* Main loop */
    char prompt[256];
    snprintf(prompt, sizeof(prompt),
             BCYN "%s" RST DIM " > " RST, g_db);

    while (1) {
        char *input = readline(prompt);
        if (!input) break; /* EOF / Ctrl-D */

        /* Trim whitespace */
        char *s = input;
        while (*s == ' ' || *s == '\t') s++;
        size_t len = strlen(s);
        while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\n'))
            s[--len] = '\0';

        if (len == 0) { free(input); continue; }

        add_history(s);

        /* -- Slash commands ----------------------------------------- */
        if (s[0] == '/' || s[0] == '\\') {
            if (strcasecmp(s, "/quit") == 0 || strcasecmp(s, "/exit") == 0 ||
                strcasecmp(s, "/q") == 0 || strcasecmp(s, "\\q") == 0) {
                free(input);
                break;
            }

            if (strcasecmp(s, "/help") == 0 || strcasecmp(s, "/h") == 0 ||
                strcasecmp(s, "/?") == 0) {
                print_help();
                free(input);
                continue;
            }

            if (strcasecmp(s, "/schema") == 0) {
                send_line("SCHEMA");
                char line[65536];
                if (recv_line(line, sizeof(line), 15) >= 0) {
                    if (strncmp(line, "SCHEMA_OK ", 10) == 0) {
                        printf("\n  " BYEL "%s" RST "\n", L->schema_title);
                        const char *p = line + 10;
                        while (*p) {
                            printf("  " DIM "|" RST " ");
                            while (*p && !(*p == '\\' && *(p + 1) == 'n')) {
                                putchar(*p++);
                            }
                            putchar('\n');
                            if (*p == '\\' && *(p + 1) == 'n') p += 2;
                        }
                        printf("\n");
                    } else {
                        print_error(L->schema_error);
                    }
                }
                free(input);
                continue;
            }

            if (strncasecmp(s, "/db ", 4) == 0) {
                const char *newdb = s + 4;
                while (*newdb == ' ') newdb++;
                if (*newdb) {
                    char cmd[512];
                    snprintf(cmd, sizeof(cmd), "DATABASE %s", newdb);
                    send_line(cmd);
                    char tmp[256];
                    recv_line(tmp, sizeof(tmp), 10);
                    strncpy(g_db, newdb, sizeof(g_db) - 1);
                    snprintf(prompt, sizeof(prompt),
                             BCYN "%s" RST DIM " > " RST, g_db);
                    char msg[512];
                    snprintf(msg, sizeof(msg), L->db_changed, g_db);
                    print_info(msg);
                } else {
                    print_error(L->err_empty_db);
                }
                free(input);
                continue;
            }

            if (strncasecmp(s, "/sql ", 5) == 0) {
                const char *sql = s + 5;
                while (*sql == ' ') sql++;
                if (*sql) {
                    char cmd[65600];
                    snprintf(cmd, sizeof(cmd), "SQL %s", sql);
                    send_line(cmd);
                    char line[65536];
                    if (recv_line(line, sizeof(line), 10) >= 0) {
                        if (strncmp(line, "SQL_PROPOSAL ", 13) == 0)
                            do_confirm(line + 13);
                        else
                            print_info(line);
                    }
                } else {
                    print_error(L->err_empty_sql);
                }
                free(input);
                continue;
            }

            /* /tables — direct SQL */
            if (strcasecmp(s, "/tables") == 0) {
                exec_direct_sql("SHOW TABLES");
                free(input);
                continue;
            }

            /* /schemas — direct SQL */
            if (strcasecmp(s, "/schemas") == 0) {
                exec_direct_sql("SHOW SCHEMAS");
                free(input);
                continue;
            }

            /* /databases — direct SQL */
            if (strcasecmp(s, "/databases") == 0) {
                exec_direct_sql("SHOW DATABASES");
                free(input);
                continue;
            }

            /* /users — direct SQL */
            if (strcasecmp(s, "/users") == 0) {
                exec_direct_sql("SELECT * FROM information_schema.users");
                free(input);
                continue;
            }

            /* /grants — direct SQL */
            if (strcasecmp(s, "/grants") == 0) {
                exec_direct_sql("SELECT * FROM information_schema.grants");
                free(input);
                continue;
            }

            /* /describe <table> or /desc <table> */
            if (strncasecmp(s, "/describe ", 10) == 0 ||
                strncasecmp(s, "/desc ", 6) == 0) {
                const char *tbl = s + (strncasecmp(s, "/desc ", 6) == 0 ? 6 : 10);
                while (*tbl == ' ') tbl++;
                if (*tbl) {
                    char sql[1024];
                    snprintf(sql, sizeof(sql), "DESCRIBE %s", tbl);
                    exec_direct_sql(sql);
                } else {
                    print_error(L->err_empty_sql);
                }
                free(input);
                continue;
            }

            /* /ai-model — re-show provider menu */
            if (strcasecmp(s, "/ai-model") == 0) {
                provider_menu(&g_provider);
                config_save(&g_provider);
                send_provider_cmd(&g_provider);
                free(input);
                continue;
            }

            char msg[256];
            snprintf(msg, sizeof(msg), "%s", L->err_unknown_cmd);
            printf("  " BRED "x" RST " %s\n", msg);
            free(input);
            continue;
        }

        /* -- Natural language input --------------------------------- */
        printf("\n  " DIM "%s" RST, L->thinking);
        fflush(stdout);

        char cmd[65600];
        snprintf(cmd, sizeof(cmd), "NL %s", s);
        send_line(cmd);

        char line[65536];
        int rc = recv_line(line, sizeof(line), 120);
        printf("\r\033[K");

        if (rc < 0) {
            print_error(L->timeout);
            free(input);
            continue;
        }

        if (strncmp(line, "SQL_PROPOSAL ", 13) == 0) {
            do_confirm(line + 13);
        }
        else if (strncmp(line, "QUESTION ", 9) == 0) {
            print_question(line + 9);
        }
        else if (strncmp(line, "ERROR", 5) == 0) {
            print_error(line + 6);
        }
        else {
            print_info(line);
        }

        free(input);
    }

    /* Close connection */
    send_line("QUIT");
    { char tmp[256]; recv_line(tmp, sizeof(tmp), 5); }
    close(g_sock);
    write_history(g_histfile);

    /* Stop child nl-service if we started it */
    if (g_service_pid > 0) {
        kill(g_service_pid, SIGTERM);
        waitpid(g_service_pid, NULL, 0);
    }

    printf("\n  " DIM "%s" RST "\n\n", L->goodbye);
    return 0;
}

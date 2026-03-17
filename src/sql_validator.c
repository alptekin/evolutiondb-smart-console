#include "sql_validator.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

/* ── helpers ─────────────────────────────────────────────────────────── */

static int starts_with_ci(const char *s, const char *prefix)
{
    return strncasecmp(s, prefix, strlen(prefix)) == 0;
}

static const char *skip_ws(const char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* ── sql_sanitize ────────────────────────────────────────────────────── */

char *sql_sanitize(char *sql)
{
    if (!sql) return sql;

    /* trim leading whitespace */
    char *start = sql;
    while (*start && isspace((unsigned char)*start)) start++;

    /* trim trailing whitespace and semicolons */
    size_t len = strlen(start);
    while (len > 0 && (isspace((unsigned char)start[len - 1]) ||
                       start[len - 1] == ';'))
        len--;
    start[len] = '\0';

    /* block multiple statements: find first ';' inside, truncate */
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\'' || start[i] == '"') {
            /* skip quoted strings */
            char q = start[i++];
            while (i < len && start[i] != q) i++;
        } else if (start[i] == ';') {
            /* check if there's more SQL after ';' */
            const char *rest = skip_ws(start + i + 1);
            if (*rest != '\0') {
                /* multiple statements — truncate to first */
                start[i] = '\0';
                len = i;
            }
            break;
        }
    }

    return start;
}

/* ── sql_validate ────────────────────────────────────────────────────── */

int sql_validate(const char *sql)
{
    if (!sql) return 0;
    const char *s = skip_ws(sql);
    if (*s == '\0') return 0;

    /* whitelist */
    static const char *whitelist[] = {
        "SELECT ",      "INSERT ",      "UPDATE ",      "DELETE ",
        "CREATE ",      "ALTER ",       "DROP ",
        "GRANT ",       "REVOKE ",
        "SHOW ",        "DESCRIBE ",
        "BEGIN",        "COMMIT",       "ROLLBACK",
        "EXPLAIN ",     "USE ",
        NULL
    };
    for (int i = 0; whitelist[i]; i++) {
        if (starts_with_ci(s, whitelist[i]))
            return 1;
    }

    return 0;
}

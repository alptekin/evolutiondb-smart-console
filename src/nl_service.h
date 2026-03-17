#ifndef NL_SERVICE_H
#define NL_SERVICE_H

#include "provider.h"
#include <stddef.h>

/* ── Session limits ───────────────────────────────────────────────────── */
#define NL_MAX_SQL        65536
#define NL_MAX_MESSAGES   256
#define NL_MAX_TURNS      40

/* ── Session ──────────────────────────────────────────────────────────── */

typedef struct {
    /* Provider (per-session, set by PROVIDER command) */
    ProviderConfig provider;

    /* Conversation history (role + content pairs) */
    int     n_messages;
    char   *msg_roles[NL_MAX_MESSAGES];
    char   *msg_contents[NL_MAX_MESSAGES];
    int     turn_count;

    /* Pending SQL */
    char    pending_sql[NL_MAX_SQL];
    int     awaiting_confirm;

    /* EvoSQL connection info */
    int     evo_sock;
    char    evo_database[256];
    char    evo_schema[256];

    /* Schema cache */
    char   *schema_text;
    int     schema_len;
} NLSession;

/* ── Global lifecycle ─────────────────────────────────────────────────── */
int  nl_service_init(void);
void nl_service_cleanup(void);

/* ── Session lifecycle ────────────────────────────────────────────────── */
NLSession *nl_session_create(void);
void       nl_session_destroy(NLSession *s);

/* ── Core inference ───────────────────────────────────────────────────── */

/*
 * Process natural-language input, return one of:
 *   "QUESTION <text>"       — LLM needs clarification
 *   "SQL_PROPOSAL <sql>"    — SQL ready for confirmation
 *   "ERROR <msg>"           — something went wrong
 *
 * Caller owns the returned string (must free).
 */
char *nl_process_input(NLSession *s, const char *user_text);

/* ── Configuration (from env vars) ────────────────────────────────────── */
typedef struct {
    int         port;               /* NL service port (default 9970)  */
    const char *evo_host;           /* EvoSQL host (127.0.0.1)         */
    int         evo_port;           /* EvoSQL EVO port (9967)          */
    const char *evo_user;           /* EvoSQL user (admin)             */
    const char *evo_password;       /* EvoSQL password (admin)         */

    /* Default provider (from env, overridden by PROVIDER command) */
    ProviderConfig default_provider;
} NLConfig;

NLConfig nl_config_from_env(void);

#endif /* NL_SERVICE_H */

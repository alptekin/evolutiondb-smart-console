#include "nl_service.h"
#include "schema_client.h"
#include "sql_validator.h"
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── System prompt ────────────────────────────────────────────────────── */

static const char *SYSTEM_PROMPT_TEMPLATE =
    "You are EvoSQL Assistant. Convert natural language to SQL for the EvoSQL database.\n"
    "You understand all languages natively. Reply in the user's language when asking questions.\n"
    "\n"
    "OUTPUT RULES:\n"
    "1. Output ONLY a ```sql``` code block when SQL is ready, nothing else.\n"
    "2. If ambiguous, ask ONE short question (no code block).\n"
    "\n"
    "EvoSQL SQL DIALECT:\n"
    "- Identifiers: plain names, NO backticks, NO double quotes.\n"
    "- Types: INT, BIGINT, VARCHAR(n), TEXT, BOOLEAN, DATE, FLOAT, DOUBLE, DECIMAL, UUID.\n"
    "- VARCHAR MUST have a length, e.g. VARCHAR(100).\n"
    "- AUTO_INCREMENT is supported for integer primary keys.\n"
    "- FOREIGN KEY supported: FOREIGN KEY (col) REFERENCES other_table(col).\n"
    "- No MySQL-specific syntax (ENGINE, CHARSET, AUTO_INCREMENT=N, IF NOT EXISTS, etc.).\n"
    "- No PostgreSQL-specific syntax (SERIAL, RETURNING, ON CONFLICT, etc.).\n"
    "\n"
    "SUPPORTED STATEMENTS:\n"
    "- DDL: CREATE TABLE, ALTER TABLE, DROP TABLE, CREATE INDEX, DROP INDEX\n"
    "- DML: SELECT, INSERT, UPDATE, DELETE\n"
    "- Queries: SHOW TABLES, SHOW DATABASES, SHOW SCHEMAS, DESCRIBE <table>\n"
    "- Transaction: BEGIN, COMMIT, ROLLBACK\n"
    "- Other: EXPLAIN <query>, USE <database>\n"
    "\n"
    "CONVENTIONS:\n"
    "- If a table does not exist in the schema below, generate CREATE TABLE.\n"
    "- If the table exists, use appropriate DML (SELECT, INSERT, UPDATE, DELETE).\n"
    "- Always add PRIMARY KEY to id columns in CREATE TABLE.\n"
    "- For listing tables use: SHOW TABLES\n"
    "- For listing databases use: SHOW DATABASES\n"
    "- For table structure use: DESCRIBE <table_name>\n";

/* ── Provider dispatch ────────────────────────────────────────────────── */

Provider *provider_get(ProviderType type)
{
    switch (type) {
    case PROVIDER_OLLAMA: return &g_provider_ollama;
    case PROVIDER_CLAUDE: return &g_provider_claude;
    case PROVIDER_OPENAI: return &g_provider_openai;
    default:              return &g_provider_ollama;
    }
}

const char *provider_default_model(ProviderType type)
{
    switch (type) {
    case PROVIDER_OLLAMA: return "qwen2.5:7b";
    case PROVIDER_CLAUDE: return "claude-sonnet-4-20250514";
    case PROVIDER_OPENAI: return "gpt-4o-mini";
    default:              return "qwen2.5:7b";
    }
}

const char *provider_default_url(ProviderType type)
{
    switch (type) {
    case PROVIDER_OLLAMA: {
        /* OLLAMA_HOST env var takes priority */
        const char *env = getenv("OLLAMA_HOST");
        return (env && *env) ? env : "http://localhost:11434";
    }
    case PROVIDER_CLAUDE: return "https://api.anthropic.com";
    case PROVIDER_OPENAI: return "https://api.openai.com";
    default:              return "http://localhost:11434";
    }
}

/* ── Config from environment ──────────────────────────────────────────── */

static const char *env_or(const char *name, const char *def)
{
    const char *v = getenv(name);
    return (v && *v) ? v : def;
}

static int env_int(const char *name, int def)
{
    const char *v = getenv(name);
    return (v && *v) ? atoi(v) : def;
}

NLConfig nl_config_from_env(void)
{
    NLConfig c;
    memset(&c, 0, sizeof(c));

    c.port         = env_int("EVOSQL_NL_PORT", 9970);
    c.evo_host     = env_or("EVOSQL_HOST", "127.0.0.1");
    c.evo_port     = env_int("EVOSQL_PORT", 9967);
    c.evo_user     = env_or("EVOSQL_USER", "admin");
    c.evo_password = env_or("EVOSQL_PASSWORD", "admin");

    /* default provider from env */
    const char *prov = env_or("EVOSQL_NL_PROVIDER", "ollama");
    if (strcasecmp(prov, "claude") == 0)
        c.default_provider.type = PROVIDER_CLAUDE;
    else if (strcasecmp(prov, "openai") == 0)
        c.default_provider.type = PROVIDER_OPENAI;
    else
        c.default_provider.type = PROVIDER_OLLAMA;

    const char *model = env_or("EVOSQL_NL_MODEL", "");
    if (model[0])
        strncpy(c.default_provider.model, model, sizeof(c.default_provider.model) - 1);

    const char *key = env_or("EVOSQL_NL_API_KEY", "");
    if (key[0])
        strncpy(c.default_provider.api_key, key, sizeof(c.default_provider.api_key) - 1);

    const char *url = env_or("EVOSQL_NL_BASE_URL", "");
    if (url[0])
        strncpy(c.default_provider.base_url, url, sizeof(c.default_provider.base_url) - 1);
    else {
        const char *ollama_host = env_or("OLLAMA_HOST", "");
        if (ollama_host[0] && c.default_provider.type == PROVIDER_OLLAMA)
            strncpy(c.default_provider.base_url, ollama_host,
                    sizeof(c.default_provider.base_url) - 1);
    }

    return c;
}

/* ── Global init/cleanup ──────────────────────────────────────────────── */

int nl_service_init(void)
{
    http_client_init();
    fprintf(stderr, "[nl_service] initialized (provider-based, no model preload)\n");
    return 0;
}

void nl_service_cleanup(void)
{
    http_client_cleanup();
}

/* ── Session lifecycle ────────────────────────────────────────────────── */

NLSession *nl_session_create(void)
{
    NLSession *s = calloc(1, sizeof(NLSession));
    if (!s) return NULL;

    s->evo_sock = -1;
    s->schema_text = NULL;
    s->schema_len = 0;
    strcpy(s->evo_database, "testdb");
    strcpy(s->evo_schema, "public");

    /* default provider will be set by PROVIDER command or from env */
    s->provider.type = PROVIDER_OLLAMA;

    return s;
}

void nl_session_destroy(NLSession *s)
{
    if (!s) return;
    if (s->evo_sock >= 0) evo_disconnect(s->evo_sock);
    free(s->schema_text);

    /* free conversation history */
    for (int i = 0; i < s->n_messages; i++) {
        free(s->msg_roles[i]);
        free(s->msg_contents[i]);
    }

    free(s);
}

/* ── Conversation management ──────────────────────────────────────────── */

static void conv_add_message(NLSession *s, const char *role, const char *content)
{
    if (s->n_messages >= NL_MAX_MESSAGES) {
        /* drop oldest non-system messages to make room */
        int start = (s->n_messages > 0 && strcmp(s->msg_roles[0], "system") == 0) ? 1 : 0;
        free(s->msg_roles[start]);
        free(s->msg_contents[start]);
        memmove(&s->msg_roles[start], &s->msg_roles[start + 1],
                (s->n_messages - start - 1) * sizeof(char *));
        memmove(&s->msg_contents[start], &s->msg_contents[start + 1],
                (s->n_messages - start - 1) * sizeof(char *));
        s->n_messages--;
    }

    s->msg_roles[s->n_messages] = strdup(role);
    s->msg_contents[s->n_messages] = strdup(content);
    s->n_messages++;
}

static void conv_set_system(NLSession *s, const char *content)
{
    /* replace system message at index 0, or insert it */
    if (s->n_messages > 0 && strcmp(s->msg_roles[0], "system") == 0) {
        free(s->msg_contents[0]);
        s->msg_contents[0] = strdup(content);
    } else {
        /* shift everything right */
        if (s->n_messages >= NL_MAX_MESSAGES) {
            /* drop last message */
            s->n_messages--;
            free(s->msg_roles[s->n_messages]);
            free(s->msg_contents[s->n_messages]);
        }
        memmove(&s->msg_roles[1], &s->msg_roles[0], s->n_messages * sizeof(char *));
        memmove(&s->msg_contents[1], &s->msg_contents[0], s->n_messages * sizeof(char *));
        s->msg_roles[0] = strdup("system");
        s->msg_contents[0] = strdup(content);
        s->n_messages++;
    }
}

/* ── Build system prompt with schema ──────────────────────────────────── */

static char *build_system_prompt(NLSession *s)
{
    size_t sys_len = strlen(SYSTEM_PROMPT_TEMPLATE);
    size_t schema_len = s->schema_text ? (size_t)s->schema_len : 0;

    /* allow more schema for better models */
    size_t schema_cap = 4096;
    if (schema_len > schema_cap) schema_len = schema_cap;

    size_t total = sys_len + schema_len + 256;
    char *prompt = malloc(total);
    if (!prompt) return NULL;

    int off = 0;
    off += snprintf(prompt + off, total - off, "%s", SYSTEM_PROMPT_TEMPLATE);

    if (s->schema_text && schema_len > 0) {
        off += snprintf(prompt + off, total - off,
                        "\nCURRENT SCHEMA (database: %s):\n%.*s\n",
                        s->evo_database, (int)schema_len, s->schema_text);
    }

    return prompt;
}

/* ── SQL extraction ───────────────────────────────────────────────────── */

static char *extract_sql_block(const char *text)
{
    const char *start = strstr(text, "```sql");
    if (!start) start = strstr(text, "```SQL");
    if (!start) start = strstr(text, "```");
    if (!start) return NULL;

    /* skip past ``` marker + optional "sql" */
    start += 3;
    if (strncasecmp(start, "sql", 3) == 0) start += 3;
    while (*start == ' ' || *start == '\n' || *start == '\r') start++;

    const char *end = strstr(start, "```");
    if (!end) end = start + strlen(start);

    while (end > start && (end[-1] == ' ' || end[-1] == '\n' || end[-1] == '\r'))
        end--;

    if (end <= start) return NULL;
    return strndup(start, end - start);
}

static int is_question(const char *text)
{
    return strchr(text, '?') != NULL;
}

/* ── Public: process NL input ─────────────────────────────────────────── */

char *nl_process_input(NLSession *s, const char *user_text)
{
    if (!s || !user_text) {
        return strdup("ERROR invalid input");
    }

    if (s->turn_count >= NL_MAX_TURNS) {
        return strdup("ERROR max turns exceeded, start a new session");
    }

    /* update system prompt with latest schema */
    char *sys_prompt = build_system_prompt(s);
    if (sys_prompt) {
        conv_set_system(s, sys_prompt);
        free(sys_prompt);
    }

    /* add user message */
    conv_add_message(s, "user", user_text);
    s->turn_count++;

    /* build ChatMessage array for provider */
    ChatMessage *msgs = malloc(s->n_messages * sizeof(ChatMessage));
    for (int i = 0; i < s->n_messages; i++) {
        msgs[i].role = s->msg_roles[i];
        msgs[i].content = s->msg_contents[i];
    }

    /* call provider */
    Provider *prov = provider_get(s->provider.type);
    char *llm_response = NULL;
    int rc = prov->chat(&s->provider, msgs, s->n_messages, &llm_response);
    free(msgs);

    if (rc != 0 || !llm_response || strlen(llm_response) == 0) {
        free(llm_response);
        return strdup("ERROR inference failed");
    }

    /* add assistant response to history */
    conv_add_message(s, "assistant", llm_response);

    /* try to extract SQL */
    char *sql = extract_sql_block(llm_response);

    if (sql) {
        char *clean = sql_sanitize(sql);
        /* flatten newlines to spaces for single-line protocol */
        for (char *p = clean; *p; p++) {
            if (*p == '\n' || *p == '\r') *p = ' ';
        }
        if (sql_validate(clean)) {
            strncpy(s->pending_sql, clean, NL_MAX_SQL - 1);
            s->pending_sql[NL_MAX_SQL - 1] = '\0';
            s->awaiting_confirm = 1;

            char *result = malloc(strlen(clean) + 32);
            sprintf(result, "SQL_PROPOSAL %s", clean);
            free(sql);
            free(llm_response);
            return result;
        } else {
            free(sql);
            free(llm_response);
            return strdup("ERROR generated SQL is not in the allowed whitelist");
        }
    }

    /* flatten newlines for single-line protocol */
    for (char *p = llm_response; *p; p++) {
        if (*p == '\n' || *p == '\r') *p = ' ';
    }

    /* check if LLM is asking a question */
    if (is_question(llm_response)) {
        char *result = malloc(strlen(llm_response) + 32);
        sprintf(result, "QUESTION %s", llm_response);
        free(llm_response);
        return result;
    }

    /* fallback — treat as question */
    char *result = malloc(strlen(llm_response) + 32);
    sprintf(result, "QUESTION %s", llm_response);
    free(llm_response);
    return result;
}

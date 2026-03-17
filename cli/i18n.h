#ifndef I18N_H
#define I18N_H

/* ── Localized string table ───────────────────────────────────────────── */

typedef struct {
    /* Banner */
    const char *app_title;
    const char *app_subtitle;

    /* Provider menu */
    const char *provider_title;
    const char *provider_ollama;
    const char *provider_groq;
    const char *provider_claude;
    const char *provider_openai;
    const char *provider_gemini;
    const char *provider_custom;
    const char *provider_prompt;
    const char *api_key_prompt;
    const char *model_prompt;       /* format: "Model (Enter = %s): " */
    const char *url_prompt;         /* format: "URL (Enter = %s): " */
    const char *provider_saved;

    /* Connection */
    const char *connecting;         /* format: "... %s:%d..." */
    const char *connected;
    const char *connection_error;
    const char *service_not_running;
    const char *database_label;

    /* Help / Commands */
    const char *cmd_title;
    const char *cmd_natural;
    const char *cmd_schema;
    const char *cmd_db;
    const char *cmd_sql;
    const char *cmd_tables;
    const char *cmd_schemas;
    const char *cmd_databases;
    const char *cmd_users;
    const char *cmd_grants;
    const char *cmd_describe;
    const char *cmd_ai_model;
    const char *cmd_help;
    const char *cmd_quit;

    /* SQL confirm */
    const char *confirm_execute;
    const char *confirm_edit;
    const char *confirm_cancel;
    const char *after_sql_title;

    /* Editor */
    const char *editor_title;
    const char *editor_save;
    const char *editor_cancel;
    const char *editor_move;
    const char *editor_newline;
    const char *editor_indent;
    const char *editor_line_col;    /* format: "Line %d, Column %d" */
    const char *editor_ai;          /* "Ask AI" */
    const char *editor_ai_title;    /* "AI — Edit your SQL with AI" */
    const char *editor_ai_current;  /* "Current SQL:" */
    const char *editor_ai_no_resp;  /* "AI did not respond" */
    const char *editor_ai_back;     /* "(Enter to return)" */

    /* Status */
    const char *thinking;
    const char *executing;
    const char *timeout;
    const char *cancelled;
    const char *goodbye;
    const char *schema_title;
    const char *schema_error;
    const char *db_changed;         /* format: "... %s" */

    /* Errors */
    const char *err_unknown_cmd;
    const char *err_empty_sql;
    const char *err_empty_db;

    /* Startup checks */
    const char *ollama_checking;        /* "Checking Ollama..." */
    const char *ollama_found;           /* "Ollama v%s" */
    const char *ollama_not_found;       /* "Ollama is not installed." */
    const char *ollama_install_ask;     /* "Would you like to install Ollama? (y/n): " */
    const char *ollama_installing;      /* "Installing Ollama..." */
    const char *ollama_install_ok;      /* "Ollama installed successfully." */
    const char *ollama_install_fail;    /* "Ollama installation failed." */
    const char *ollama_install_manual;  /* "Install manually: https://ollama.com" */
    const char *ollama_not_running;     /* "Ollama is not running." */
    const char *ollama_starting;        /* "Starting Ollama..." */
    const char *ollama_connected;       /* "Ollama ready" */
    const char *ollama_run_docker;      /* "Start Ollama with Docker:" */
    const char *ollama_or_manual;       /* "Or select a different provider below." */
    const char *ollama_no_models;       /* "No models found." */
    const char *ollama_pull_hint;       /* "Pull a model first:" */
    const char *ollama_models_title;    /* "Available models:" */
    const char *evosql_checking;        /* "Checking EvoSQL..." */
    const char *evosql_found;           /* "EvoSQL is running" */
    const char *evosql_not_found;       /* "EvoSQL is not running." */
    const char *evosql_docker_hint;     /* "Start with Docker:" */
    const char *service_starting;       /* "Starting NL service..." */
    const char *service_ready;          /* "NL service ready" */
    const char *service_start_fail;     /* "Could not start NL service." */

    /* Examples */
    const char *example1;
    const char *example2;
    const char *example3;
    const char *example4;
} I18nStrings;

/* ── English ──────────────────────────────────────────────────────────── */

static const I18nStrings I18N_EN = {
    .app_title          = "EvoSQL Natural Language Assistant",
    .app_subtitle       = "Generate, edit and execute SQL with natural language",

    .provider_title     = "Select LLM Provider:",
    .provider_ollama    = "Ollama (local, free)",
    .provider_groq      = "Groq (free, fast)",
    .provider_claude    = "Claude (API key required)",
    .provider_openai    = "OpenAI (API key required)",
    .provider_gemini    = "Gemini (free)",
    .provider_custom    = "Custom endpoint",
    .provider_prompt    = "Your choice (Enter = 1):",
    .api_key_prompt     = "API Key:",
    .model_prompt       = "Model (Enter = %s):",
    .url_prompt         = "URL (Enter = %s):",
    .provider_saved     = "Provider saved.",

    .connecting         = "Connecting to %s:%d...",
    .connected          = "Connected",
    .connection_error   = "Connection error!",
    .service_not_running = "Is NL service running?",
    .database_label     = "Database:",

    .cmd_title          = "Commands:",
    .cmd_natural        = "Type in natural language",
    .cmd_schema         = "Show current tables",
    .cmd_db             = "Switch database",
    .cmd_sql            = "Send SQL directly",
    .cmd_tables         = "List tables",
    .cmd_schemas        = "List schemas",
    .cmd_databases      = "List databases",
    .cmd_users          = "List users",
    .cmd_grants         = "List grants",
    .cmd_describe       = "Describe table",
    .cmd_ai_model       = "Change AI provider",
    .cmd_help           = "This help message",
    .cmd_quit           = "Quit",

    .confirm_execute    = "Execute",
    .confirm_edit       = "Edit",
    .confirm_cancel     = "Cancel",
    .after_sql_title    = "After SQL is generated:",

    .editor_title       = "SQL Editor",
    .editor_save        = "Save and execute",
    .editor_cancel      = "Cancel",
    .editor_move        = "Move",
    .editor_newline     = "New line",
    .editor_indent      = "Indent",
    .editor_line_col    = "Line %d, Column %d",
    .editor_ai          = "Ask AI",
    .editor_ai_title    = "AI \xe2\x80\x94 Edit your SQL with AI",
    .editor_ai_current  = "Current SQL:",
    .editor_ai_no_resp  = "AI did not respond",
    .editor_ai_back     = "(Enter to return)",

    .thinking           = "Thinking...",
    .executing          = "Executing...",
    .timeout            = "Timeout \xe2\x80\x94 no response",
    .cancelled          = "Cancelled.",
    .goodbye            = "Goodbye!",
    .schema_title       = "Current Schema:",
    .schema_error       = "Cannot fetch schema. Is EvoSQL running?",
    .db_changed         = "Database changed: %s",

    .err_unknown_cmd    = "Unknown command. Type /help.",
    .err_empty_sql      = "Use: /sql <SQL statement>",
    .err_empty_db       = "Use: /db <database_name>",

    .ollama_checking    = "Checking Ollama...",
    .ollama_found       = "Ollama v%s",
    .ollama_not_found   = "Ollama is not installed.",
    .ollama_install_ask = "Would you like to install Ollama? (y/n): ",
    .ollama_installing  = "Installing Ollama...",
    .ollama_install_ok  = "Ollama installed successfully.",
    .ollama_install_fail = "Ollama installation failed.",
    .ollama_install_manual = "Install manually: https://ollama.com",
    .ollama_not_running = "Ollama is installed but not running.",
    .ollama_starting    = "Starting Ollama...",
    .ollama_connected   = "Ollama ready",
    .ollama_run_docker  = "Start Ollama with Docker:",
    .ollama_or_manual   = "Or select a different provider below.",
    .ollama_no_models   = "Ollama is running but no models found.",
    .ollama_pull_hint   = "Pull a model first:",
    .ollama_models_title = "Available models:",
    .evosql_checking    = "Checking EvoSQL...",
    .evosql_found       = "EvoSQL is running",
    .evosql_not_found   = "EvoSQL is not running.",
    .evosql_docker_hint = "Start with Docker:",
    .service_starting   = "Starting NL service...",
    .service_ready      = "NL service ready",
    .service_start_fail = "Could not start NL service.",

    .example1           = "create a users table with id and name",
    .example2           = "show all users",
    .example3           = "delete user with id 5",
    .example4           = "create a products table with id, name and price",
};

/* ── T\xc3\xbcrk\xc3\xa7e ─────────────────────────────────────────────────────── */

static const I18nStrings I18N_TR = {
    .app_title          = "EvoSQL Do" "\xc4\x9f" "al Dil Asistan" "\xc4\xb1",
    .app_subtitle       = "Do" "\xc4\x9f" "al dil ile SQL " "\xc3\xbc" "retin, d" "\xc3\xbc" "zenleyin ve " "\xc3\xa7" "al" "\xc4\xb1" "\xc5\x9f" "t" "\xc4\xb1" "r" "\xc4\xb1" "n",

    .provider_title     = "LLM Sa" "\xc4\x9f" "lay" "\xc4\xb1" "c" "\xc4\xb1" " se" "\xc3\xa7" "in:",
    .provider_ollama    = "Ollama (yerel, " "\xc3\xbc" "cretsiz)",
    .provider_groq      = "Groq (" "\xc3\xbc" "cretsiz, h" "\xc4\xb1" "zl" "\xc4\xb1" ")",
    .provider_claude    = "Claude (API key gerekli)",
    .provider_openai    = "OpenAI (API key gerekli)",
    .provider_gemini    = "Gemini (" "\xc3\xbc" "cretsiz)",
    .provider_custom    = "\xc3\x96" "zel endpoint",
    .provider_prompt    = "Se" "\xc3\xa7" "iminiz (Enter = 1):",
    .api_key_prompt     = "API Key:",
    .model_prompt       = "Model (Enter = %s):",
    .url_prompt         = "URL (Enter = %s):",
    .provider_saved     = "Sa" "\xc4\x9f" "lay" "\xc4\xb1" "c" "\xc4\xb1" " kaydedildi.",

    .connecting         = "Ba" "\xc4\x9f" "lan" "\xc4\xb1" "l" "\xc4\xb1" "yor %s:%d...",
    .connected          = "Ba" "\xc4\x9f" "land" "\xc4\xb1",
    .connection_error   = "Ba" "\xc4\x9f" "lant" "\xc4\xb1" " hatas" "\xc4\xb1" "!",
    .service_not_running = "NL servisi " "\xc3\xa7" "al" "\xc4\xb1" "\xc5\x9f" "\xc4\xb1" "yor mu?",
    .database_label     = "Veritaban" "\xc4\xb1" ":",

    .cmd_title          = "Komutlar:",
    .cmd_natural        = "Do" "\xc4\x9f" "al dilde yaz" "\xc4\xb1" "n",
    .cmd_schema         = "Mevcut tablolar" "\xc4\xb1" " g" "\xc3\xb6" "ster",
    .cmd_db             = "Veritaban" "\xc4\xb1" " de" "\xc4\x9f" "i" "\xc5\x9f" "tir",
    .cmd_sql            = "SQL'i do" "\xc4\x9f" "rudan g" "\xc3\xb6" "nder",
    .cmd_tables         = "Tablolar" "\xc4\xb1" " listele",
    .cmd_schemas        = "\xc5\x9e" "emalar" "\xc4\xb1" " listele",
    .cmd_databases      = "Veritabanlar" "\xc4\xb1" "n" "\xc4\xb1" " listele",
    .cmd_users          = "Kullan" "\xc4\xb1" "c" "\xc4\xb1" "lar" "\xc4\xb1" " listele",
    .cmd_grants         = "Yetkileri listele",
    .cmd_describe       = "Tablo yap" "\xc4\xb1" "s" "\xc4\xb1" "n" "\xc4\xb1" " g" "\xc3\xb6" "ster",
    .cmd_ai_model       = "AI sa" "\xc4\x9f" "lay" "\xc4\xb1" "c" "\xc4\xb1" "y" "\xc4\xb1" " de" "\xc4\x9f" "i" "\xc5\x9f" "tir",
    .cmd_help           = "Bu yard" "\xc4\xb1" "m mesaj" "\xc4\xb1",
    .cmd_quit           = "\xc3\x87" "\xc4\xb1" "k" "\xc4\xb1" "\xc5\x9f",

    .confirm_execute    = "\xc3\x87" "al" "\xc4\xb1" "\xc5\x9f" "t" "\xc4\xb1" "r",
    .confirm_edit       = "D" "\xc3\xbc" "zenle",
    .confirm_cancel     = "\xc4\xb0" "ptal",
    .after_sql_title    = "SQL " "\xc3\xbc" "retildikten sonra:",

    .editor_title       = "SQL D" "\xc3\xbc" "zenleyici",
    .editor_save        = "Kaydet ve " "\xc3\xa7" "al" "\xc4\xb1" "\xc5\x9f" "t" "\xc4\xb1" "r",
    .editor_cancel      = "\xc4\xb0" "ptal",
    .editor_move        = "Hareket",
    .editor_newline     = "Yeni sat" "\xc4\xb1" "r",
    .editor_indent      = "Girinti",
    .editor_line_col    = "Sat" "\xc4\xb1" "r %d, S" "\xc3\xbc" "tun %d",
    .editor_ai          = "AI'a sor",
    .editor_ai_title    = "AI \xe2\x80\x94 SQL'inizi AI ile d" "\xc3\xbc" "zenleyin",
    .editor_ai_current  = "Mevcut SQL:",
    .editor_ai_no_resp  = "AI yan" "\xc4\xb1" "t vermedi",
    .editor_ai_back     = "(D" "\xc3\xb6" "nmek i" "\xc3\xa7" "in Enter)",

    .thinking           = "D" "\xc3\xbc" "\xc5\x9f" "\xc3\xbc" "n" "\xc3\xbc" "yor...",
    .executing          = "\xc3\x87" "al" "\xc4\xb1" "\xc5\x9f" "t" "\xc4\xb1" "r" "\xc4\xb1" "l" "\xc4\xb1" "yor...",
    .timeout            = "Zaman a" "\xc5\x9f" "\xc4\xb1" "m" "\xc4\xb1" " \xe2\x80\x94 yan" "\xc4\xb1" "t al" "\xc4\xb1" "namad" "\xc4\xb1",
    .cancelled          = "\xc4\xb0" "ptal edildi.",
    .goodbye            = "G" "\xc3\xbc" "le g" "\xc3\xbc" "le!",
    .schema_title       = "Mevcut " "\xc5\x9e" "ema:",
    .schema_error       = "\xc5\x9e" "ema al" "\xc4\xb1" "namad" "\xc4\xb1" ". EvoSQL " "\xc3\xa7" "al" "\xc4\xb1" "\xc5\x9f" "\xc4\xb1" "yor mu?",
    .db_changed         = "Veritaban" "\xc4\xb1" " de" "\xc4\x9f" "i" "\xc5\x9f" "tirildi: %s",

    .err_unknown_cmd    = "Bilinmeyen komut. /help yaz" "\xc4\xb1" "n.",
    .err_empty_sql      = "Kullan" "\xc4\xb1" "m: /sql <SQL ifadesi>",
    .err_empty_db       = "Kullan" "\xc4\xb1" "m: /db <veritaban" "\xc4\xb1" "_ad" "\xc4\xb1" ">",

    .ollama_checking    = "Ollama kontrol ediliyor...",
    .ollama_found       = "Ollama v%s",
    .ollama_not_found   = "Ollama y" "\xc3\xbc" "kl" "\xc3\xbc" " de" "\xc4\x9f" "il.",
    .ollama_install_ask = "Ollama y" "\xc3\xbc" "klemek ister misiniz? (e/h): ",
    .ollama_installing  = "Ollama y" "\xc3\xbc" "kleniyor...",
    .ollama_install_ok  = "Ollama ba" "\xc5\x9f" "ar" "\xc4\xb1" "yla y" "\xc3\xbc" "klendi.",
    .ollama_install_fail = "Ollama y" "\xc3\xbc" "klenemedi.",
    .ollama_install_manual = "Elle y" "\xc3\xbc" "kleyin: https://ollama.com",
    .ollama_not_running = "Ollama y" "\xc3\xbc" "kl" "\xc3\xbc" " ama " "\xc3\xa7" "al" "\xc4\xb1" "\xc5\x9f" "m" "\xc4\xb1" "yor.",
    .ollama_starting    = "Ollama ba" "\xc5\x9f" "lat" "\xc4\xb1" "l" "\xc4\xb1" "yor...",
    .ollama_connected   = "Ollama haz" "\xc4\xb1" "r",
    .ollama_run_docker  = "Docker ile Ollama ba" "\xc5\x9f" "lat" "\xc4\xb1" "n:",
    .ollama_or_manual   = "Ya da a" "\xc5\x9f" "a" "\xc4\x9f" "\xc4\xb1" "daki men" "\xc3\xbc" "den farkl" "\xc4\xb1" " bir sa" "\xc4\x9f" "lay" "\xc4\xb1" "c" "\xc4\xb1" " se" "\xc3\xa7" "in.",
    .ollama_no_models   = "Ollama " "\xc3\xa7" "al" "\xc4\xb1" "\xc5\x9f" "\xc4\xb1" "yor ama model bulunamad" "\xc4\xb1" ".",
    .ollama_pull_hint   = "\xc3\x96" "nce bir model indirin:",
    .ollama_models_title = "Mevcut modeller:",
    .evosql_checking    = "EvoSQL kontrol ediliyor...",
    .evosql_found       = "EvoSQL " "\xc3\xa7" "al" "\xc4\xb1" "\xc5\x9f" "\xc4\xb1" "yor",
    .evosql_not_found   = "EvoSQL " "\xc3\xa7" "al" "\xc4\xb1" "\xc5\x9f" "m" "\xc4\xb1" "yor.",
    .evosql_docker_hint = "Docker ile ba" "\xc5\x9f" "lat" "\xc4\xb1" "n:",
    .service_starting   = "NL servisi ba" "\xc5\x9f" "lat" "\xc4\xb1" "l" "\xc4\xb1" "yor...",
    .service_ready      = "NL servisi haz" "\xc4\xb1" "r",
    .service_start_fail = "NL servisi ba" "\xc5\x9f" "lat" "\xc4\xb1" "lamad" "\xc4\xb1" ".",

    .example1           = "kullan" "\xc4\xb1" "c" "\xc4\xb1" "lar tablosu olu" "\xc5\x9f" "tur, id ve isim olsun",
    .example2           = "t" "\xc3\xbc" "m kullan" "\xc4\xb1" "c" "\xc4\xb1" "lar" "\xc4\xb1" " g" "\xc3\xb6" "ster",
    .example3           = "id'si 5 olan kullan" "\xc4\xb1" "c" "\xc4\xb1" "y" "\xc4\xb1" " sil",
    .example4           = "\xc3\xbc" "r" "\xc3\xbc" "nler tablosu yarat, id isim fiyat olsun",
};

/* ── Locale detection & getter ────────────────────────────────────────── */

#include <string.h>
#include <stdlib.h>

static const I18nStrings *i18n_get(void)
{
    const char *locale = getenv("LC_ALL");
    if (!locale || !*locale) locale = getenv("LANG");
    if (!locale || !*locale) locale = getenv("LC_MESSAGES");
    if (!locale || !*locale) return &I18N_EN;

    if (strncmp(locale, "tr", 2) == 0) return &I18N_TR;

    return &I18N_EN;
}

#endif /* I18N_H */

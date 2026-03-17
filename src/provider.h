#ifndef PROVIDER_H
#define PROVIDER_H

/* ── Provider types ───────────────────────────────────────────────────── */

typedef enum {
    PROVIDER_OLLAMA,        /* Local Ollama (default) */
    PROVIDER_CLAUDE,        /* Anthropic Claude API   */
    PROVIDER_OPENAI         /* OpenAI-compatible: OpenAI, Groq, Copilot, custom */
} ProviderType;

typedef struct {
    ProviderType type;
    char model[256];
    char api_key[512];
    char base_url[512];
} ProviderConfig;

/* ── Chat message ─────────────────────────────────────────────────────── */

typedef struct {
    const char *role;       /* "system", "user", "assistant" */
    const char *content;
} ChatMessage;

/* ── Provider interface ───────────────────────────────────────────────── */

typedef struct {
    /*
     * Send messages to the LLM, get a response.
     * Returns 0 on success, -1 on error.
     * On success, *out_response is heap-allocated (caller frees).
     */
    int (*chat)(const ProviderConfig *cfg, ChatMessage *msgs, int n_msgs,
                char **out_response);
} Provider;

/* Get provider implementation by type */
Provider *provider_get(ProviderType type);

/* Default model name for a provider type */
const char *provider_default_model(ProviderType type);

/* Default base URL for a provider type */
const char *provider_default_url(ProviderType type);

/* ── Provider implementations (defined in provider_*.c) ───────────────── */

extern Provider g_provider_ollama;
extern Provider g_provider_claude;
extern Provider g_provider_openai;

#endif /* PROVIDER_H */

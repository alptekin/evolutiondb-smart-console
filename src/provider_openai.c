#include "provider.h"
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * OpenAI-compatible API: POST {base_url}/v1/chat/completions
 *
 * Works with: OpenAI, Groq, GitHub Copilot, any OpenAI-compatible endpoint.
 *
 * Request:
 *   {
 *     "model": "gpt-4o-mini",
 *     "messages": [{"role":"system","content":"..."},{"role":"user","content":"..."}],
 *     "max_tokens": 2048
 *   }
 *
 * Response:
 *   {"choices":[{"message":{"content":"..."}}]}
 */

static int openai_chat(const ProviderConfig *cfg, ChatMessage *msgs, int n_msgs,
                       char **out_response)
{
    /* build URL — if base already ends with a path segment (e.g. Gemini's
       .../v1beta/openai), append only /chat/completions; otherwise add
       the standard /v1/chat/completions */
    char url[1024];
    const char *base = cfg->base_url[0] ? cfg->base_url : "https://api.openai.com";
    int blen = (int)strlen(base);
    while (blen > 0 && base[blen - 1] == '/') blen--;   /* trim trailing / */

    /* Check if base already contains /v1 or similar version path after host */
    const char *path_start = strstr(base, "://");
    const char *first_slash = path_start ? strchr(path_start + 3, '/') : NULL;

    if (first_slash && strlen(first_slash) > 1) {
        /* base has a path (e.g. /v1beta/openai) — append just /chat/completions */
        snprintf(url, sizeof(url), "%.*s/chat/completions", blen, base);
    } else {
        /* standard: base is just host — append /v1/chat/completions */
        snprintf(url, sizeof(url), "%.*s/v1/chat/completions", blen, base);
    }

    /* headers */
    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", cfg->api_key);
    const char *headers[] = { auth_header };
    int n_headers = cfg->api_key[0] ? 1 : 0;

    /* build messages JSON */
    size_t cap = 4096;
    char *messages_json = malloc(cap);
    int off = 0;
    off += snprintf(messages_json + off, cap - off, "[");

    for (int i = 0; i < n_msgs; i++) {
        char *escaped = json_escape(msgs[i].content);
        size_t need = strlen(escaped) + 128;
        if ((size_t)(off + need) >= cap) {
            cap = (cap + need) * 2;
            messages_json = realloc(messages_json, cap);
        }
        off += snprintf(messages_json + off, cap - off,
                        "%s{\"role\":\"%s\",\"content\":\"%s\"}",
                        i > 0 ? "," : "",
                        msgs[i].role, escaped);
        free(escaped);
    }
    off += snprintf(messages_json + off, cap - off, "]");

    /* build request body */
    const char *model = cfg->model[0] ? cfg->model : "gpt-4o-mini";

    size_t body_cap = strlen(messages_json) + 512;
    char *body = malloc(body_cap);
    snprintf(body, body_cap,
             "{\"model\":\"%s\",\"messages\":%s,\"max_tokens\":2048}",
             model, messages_json);
    free(messages_json);

    /* POST */
    HttpResponse resp = {0};
    int rc = http_post_json(url, headers, n_headers, body, &resp);
    free(body);

    if (rc != 0) {
        http_response_free(&resp);
        return -1;
    }

    /* extract choices[0].message.content from response */
    char *content = json_extract_string(resp.data, "content");
    http_response_free(&resp);

    if (!content) {
        fprintf(stderr, "[openai] no content in response\n");
        return -1;
    }

    *out_response = content;
    return 0;
}

/* ── Provider instance ────────────────────────────────────────────────── */

Provider g_provider_openai = {
    .chat = openai_chat
};

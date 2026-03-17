#include "provider.h"
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Ollama API: POST {base_url}/api/chat
 *
 * Request:
 *   {"model":"qwen2.5:7b","messages":[{"role":"user","content":"..."}],"stream":false}
 *
 * Response:
 *   {"message":{"role":"assistant","content":"..."}}
 */

/* ── Build messages JSON array ────────────────────────────────────────── */

static char *build_messages_json(ChatMessage *msgs, int n)
{
    size_t cap = 4096;
    char *buf = malloc(cap);
    int off = 0;
    off += snprintf(buf + off, cap - off, "[");

    for (int i = 0; i < n; i++) {
        char *escaped = json_escape(msgs[i].content);
        size_t need = strlen(escaped) + strlen(msgs[i].role) + 64;
        if ((size_t)(off + need) >= cap) {
            cap = (cap + need) * 2;
            buf = realloc(buf, cap);
        }
        off += snprintf(buf + off, cap - off,
                        "%s{\"role\":\"%s\",\"content\":\"%s\"}",
                        i > 0 ? "," : "",
                        msgs[i].role, escaped);
        free(escaped);
    }

    if ((size_t)(off + 2) >= cap) { cap += 16; buf = realloc(buf, cap); }
    off += snprintf(buf + off, cap - off, "]");
    return buf;
}

/* ── Chat implementation ──────────────────────────────────────────────── */

static int ollama_chat(const ProviderConfig *cfg, ChatMessage *msgs, int n_msgs,
                       char **out_response)
{
    /* build URL */
    char url[1024];
    const char *base = cfg->base_url[0] ? cfg->base_url : "http://localhost:11434";
    snprintf(url, sizeof(url), "%s/api/chat", base);

    /* build request body */
    char *messages_json = build_messages_json(msgs, n_msgs);
    const char *model = cfg->model[0] ? cfg->model : "qwen2.5:7b";

    size_t body_cap = strlen(messages_json) + 512;
    char *body = malloc(body_cap);
    snprintf(body, body_cap,
             "{\"model\":\"%s\",\"messages\":%s,\"stream\":false}",
             model, messages_json);
    free(messages_json);

    /* POST */
    HttpResponse resp = {0};
    int rc = http_post_json(url, NULL, 0, body, &resp);
    free(body);

    if (rc != 0) {
        http_response_free(&resp);
        return -1;
    }

    /* extract message.content from response */
    char *content = json_extract_string(resp.data, "content");
    http_response_free(&resp);

    if (!content) {
        fprintf(stderr, "[ollama] no content in response\n");
        return -1;
    }

    *out_response = content;
    return 0;
}

/* ── Provider instance ────────────────────────────────────────────────── */

Provider g_provider_ollama = {
    .chat = ollama_chat
};

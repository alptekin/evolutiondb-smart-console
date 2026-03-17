#include "provider.h"
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Claude API: POST https://api.anthropic.com/v1/messages
 *
 * Request:
 *   {
 *     "model": "claude-sonnet-4-20250514",
 *     "max_tokens": 2048,
 *     "system": "...",                          ← system prompt ayrı alan
 *     "messages": [{"role":"user","content":"..."}]
 *   }
 *
 * Response:
 *   {"content":[{"type":"text","text":"..."}]}
 */

static int claude_chat(const ProviderConfig *cfg, ChatMessage *msgs, int n_msgs,
                       char **out_response)
{
    /* build URL */
    char url[1024];
    const char *base = cfg->base_url[0] ? cfg->base_url : "https://api.anthropic.com";
    snprintf(url, sizeof(url), "%s/v1/messages", base);

    /* headers */
    char key_header[600];
    snprintf(key_header, sizeof(key_header), "x-api-key: %s", cfg->api_key);
    const char *headers[] = {
        key_header,
        "anthropic-version: 2023-06-01"
    };

    /* separate system message from user/assistant messages */
    const char *system_content = NULL;
    int msg_start = 0;
    if (n_msgs > 0 && strcmp(msgs[0].role, "system") == 0) {
        system_content = msgs[0].content;
        msg_start = 1;
    }

    /* build messages JSON (excluding system) */
    size_t cap = 4096;
    char *messages_json = malloc(cap);
    int off = 0;
    off += snprintf(messages_json + off, cap - off, "[");

    for (int i = msg_start; i < n_msgs; i++) {
        char *escaped = json_escape(msgs[i].content);
        size_t need = strlen(escaped) + 128;
        if ((size_t)(off + need) >= cap) {
            cap = (cap + need) * 2;
            messages_json = realloc(messages_json, cap);
        }
        off += snprintf(messages_json + off, cap - off,
                        "%s{\"role\":\"%s\",\"content\":\"%s\"}",
                        i > msg_start ? "," : "",
                        msgs[i].role, escaped);
        free(escaped);
    }
    off += snprintf(messages_json + off, cap - off, "]");

    /* build request body */
    const char *model = cfg->model[0] ? cfg->model : "claude-sonnet-4-20250514";
    char *system_escaped = system_content ? json_escape(system_content) : NULL;

    size_t body_cap = strlen(messages_json) + (system_escaped ? strlen(system_escaped) : 0) + 512;
    char *body = malloc(body_cap);

    if (system_escaped) {
        snprintf(body, body_cap,
                 "{\"model\":\"%s\",\"max_tokens\":2048,"
                 "\"system\":\"%s\","
                 "\"messages\":%s}",
                 model, system_escaped, messages_json);
        free(system_escaped);
    } else {
        snprintf(body, body_cap,
                 "{\"model\":\"%s\",\"max_tokens\":2048,"
                 "\"messages\":%s}",
                 model, messages_json);
    }
    free(messages_json);

    /* POST */
    HttpResponse resp = {0};
    int rc = http_post_json(url, headers, 2, body, &resp);
    free(body);

    if (rc != 0) {
        http_response_free(&resp);
        return -1;
    }

    /* extract content[0].text from response */
    char *text = json_extract_string(resp.data, "text");
    http_response_free(&resp);

    if (!text) {
        fprintf(stderr, "[claude] no text in response\n");
        return -1;
    }

    *out_response = text;
    return 0;
}

/* ── Provider instance ────────────────────────────────────────────────── */

Provider g_provider_claude = {
    .chat = claude_chat
};

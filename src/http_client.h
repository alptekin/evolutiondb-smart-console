#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

/* ── HTTP response ────────────────────────────────────────────────────── */

typedef struct {
    char  *data;
    size_t len;
} HttpResponse;

/*
 * POST JSON to a URL with optional headers.
 * Returns 0 on success (HTTP 2xx), -1 on error.
 * On success, resp->data is heap-allocated (caller frees with http_response_free).
 */
int http_post_json(const char *url, const char **headers, int n_headers,
                   const char *body, HttpResponse *resp);

void http_response_free(HttpResponse *resp);

/* ── JSON helpers (simple string extraction, no cJSON dependency) ─────── */

/*
 * Extract a string value for the given key from JSON.
 * Handles nested objects by searching for "key": "value" patterns.
 * Returns heap-allocated string, or NULL if not found.
 *
 * Examples:
 *   json_extract_string(json, "content")
 *     from {"message":{"content":"hello"}} → "hello"
 *   json_extract_string(json, "text")
 *     from {"content":[{"type":"text","text":"hello"}]} → "hello"
 */
char *json_extract_string(const char *json, const char *key);

/*
 * Escape a string for JSON embedding.
 * Escapes: \ → \\, " → \", newline → \n, tab → \t, etc.
 * Returns heap-allocated string (caller frees).
 */
char *json_escape(const char *str);

/* ── Global init/cleanup ──────────────────────────────────────────────── */

void http_client_init(void);
void http_client_cleanup(void);

#endif /* HTTP_CLIENT_H */

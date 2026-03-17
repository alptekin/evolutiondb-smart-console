#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

/* ── Global init/cleanup ──────────────────────────────────────────────── */

void http_client_init(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void http_client_cleanup(void)
{
    curl_global_cleanup();
}

/* ── curl write callback ──────────────────────────────────────────────── */

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    HttpResponse *resp = (HttpResponse *)userdata;
    size_t bytes = size * nmemb;

    char *tmp = realloc(resp->data, resp->len + bytes + 1);
    if (!tmp) return 0;

    resp->data = tmp;
    memcpy(resp->data + resp->len, ptr, bytes);
    resp->len += bytes;
    resp->data[resp->len] = '\0';

    return bytes;
}

/* ── HTTP POST JSON ───────────────────────────────────────────────────── */

int http_post_json(const char *url, const char **headers, int n_headers,
                   const char *body, HttpResponse *resp)
{
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    resp->data = NULL;
    resp->len = 0;

    /* set URL */
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);

    /* headers */
    struct curl_slist *hlist = NULL;
    hlist = curl_slist_append(hlist, "Content-Type: application/json");
    for (int i = 0; i < n_headers; i++) {
        hlist = curl_slist_append(hlist, headers[i]);
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);

    /* response callback */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);

    /* timeouts */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    /* perform */
    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "[http] curl error: %s\n", curl_easy_strerror(res));
        free(resp->data);
        resp->data = NULL;
        resp->len = 0;
        return -1;
    }

    if (http_code < 200 || http_code >= 300) {
        fprintf(stderr, "[http] HTTP %ld: %.*s\n", http_code,
                (int)(resp->len > 500 ? 500 : resp->len),
                resp->data ? resp->data : "(null)");
        return -1;
    }

    return 0;
}

void http_response_free(HttpResponse *resp)
{
    free(resp->data);
    resp->data = NULL;
    resp->len = 0;
}

/* ── JSON helpers ─────────────────────────────────────────────────────── */

char *json_escape(const char *str)
{
    if (!str) return strdup("");

    size_t len = strlen(str);
    /* worst case: every char needs escaping → 2x + some extra */
    char *out = malloc(len * 2 + 16);
    if (!out) return NULL;

    int oi = 0;
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
        case '"':  out[oi++] = '\\'; out[oi++] = '"';  break;
        case '\\': out[oi++] = '\\'; out[oi++] = '\\'; break;
        case '\n': out[oi++] = '\\'; out[oi++] = 'n';  break;
        case '\r': out[oi++] = '\\'; out[oi++] = 'r';  break;
        case '\t': out[oi++] = '\\'; out[oi++] = 't';  break;
        default:
            if ((unsigned char)str[i] < 0x20) {
                oi += sprintf(out + oi, "\\u%04x", (unsigned char)str[i]);
            } else {
                out[oi++] = str[i];
            }
        }
    }
    out[oi] = '\0';
    return out;
}

char *json_extract_string(const char *json, const char *key)
{
    if (!json || !key) return NULL;

    /* search for "key" : "value" pattern */
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = json;
    while ((p = strstr(p, pattern)) != NULL) {
        p += strlen(pattern);

        /* skip whitespace and colon */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p != ':') continue;
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

        if (*p != '"') continue;
        p++; /* skip opening quote */

        /* find end of string value (handle escapes) */
        const char *start = p;
        while (*p && !(*p == '"' && *(p - 1) != '\\')) p++;

        if (*p != '"') continue;

        /* extract and unescape */
        size_t vlen = (size_t)(p - start);
        char *value = malloc(vlen + 1);
        if (!value) return NULL;

        int vi = 0;
        for (size_t i = 0; i < vlen; i++) {
            if (start[i] == '\\' && i + 1 < vlen) {
                switch (start[i + 1]) {
                case '"':  value[vi++] = '"';  i++; break;
                case '\\': value[vi++] = '\\'; i++; break;
                case 'n':  value[vi++] = '\n'; i++; break;
                case 'r':  value[vi++] = '\r'; i++; break;
                case 't':  value[vi++] = '\t'; i++; break;
                default:   value[vi++] = start[i]; break;
                }
            } else {
                value[vi++] = start[i];
            }
        }
        value[vi] = '\0';
        return value;
    }

    return NULL;
}

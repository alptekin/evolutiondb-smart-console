#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>

/* ── Provider config (matches server-side ProviderConfig) ─────────────── */

typedef struct {
    int  type;              /* 0=ollama, 1=claude, 2=openai */
    char model[256];
    char api_key[512];
    char base_url[512];
} ClientProviderConfig;

/* ── Config file path ─────────────────────────────────────────────────── */

static const char CONFIG_MAGIC[] = "EVOQ";
#define CONFIG_VERSION 1

static void config_path(char *buf, int max)
{
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }
    snprintf(buf, max, "%s/.evosql", home);
    mkdir(buf, 0700);
    snprintf(buf, max, "%s/.evosql/nl-config.bin", home);
}

/* ── XOR encryption with machine key ──────────────────────────────────── */

static void derive_key(unsigned char *key, int keylen)
{
    char hostname[256] = {0};
    gethostname(hostname, sizeof(hostname) - 1);
    const char *user = getenv("USER");
    if (!user) user = "evosql";
    const char *salt = "EvoSQL-NL-2024";

    /* simple hash: combine hostname + user + salt */
    unsigned int h = 0x5A3C9B1D;
    for (const char *p = hostname; *p; p++) h = h * 31 + (unsigned char)*p;
    for (const char *p = user; *p; p++)     h = h * 37 + (unsigned char)*p;
    for (const char *p = salt; *p; p++)     h = h * 41 + (unsigned char)*p;

    for (int i = 0; i < keylen; i++) {
        h = h * 1103515245 + 12345;
        key[i] = (unsigned char)((h >> 16) & 0xFF);
    }
}

static void xor_encrypt(unsigned char *data, int len)
{
    unsigned char key[256];
    derive_key(key, sizeof(key));
    for (int i = 0; i < len; i++)
        data[i] ^= key[i % sizeof(key)];
}

/* ── Save config ──────────────────────────────────────────────────────── */

static int config_save(const ClientProviderConfig *cfg)
{
    char path[512];
    config_path(path, sizeof(path));

    /* build payload */
    unsigned char payload[2048];
    int off = 0;
    payload[off++] = (unsigned char)cfg->type;

    int mlen = (int)strlen(cfg->model);
    memcpy(payload + off, cfg->model, mlen + 1); off += mlen + 1;

    int alen = (int)strlen(cfg->api_key);
    memcpy(payload + off, cfg->api_key, alen + 1); off += alen + 1;

    int ulen = (int)strlen(cfg->base_url);
    memcpy(payload + off, cfg->base_url, ulen + 1); off += ulen + 1;

    /* encrypt */
    xor_encrypt(payload, off);

    /* write file */
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    fwrite(CONFIG_MAGIC, 1, 4, f);
    unsigned char ver = CONFIG_VERSION;
    fwrite(&ver, 1, 1, f);
    fwrite(payload, 1, off, f);
    fclose(f);

    return 0;
}

/* ── Load config ──────────────────────────────────────────────────────── */

static int config_load(ClientProviderConfig *cfg)
{
    char path[512];
    config_path(path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    /* read header */
    char magic[4];
    unsigned char ver;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, CONFIG_MAGIC, 4) != 0) {
        fclose(f);
        return -1;
    }
    if (fread(&ver, 1, 1, f) != 1 || ver != CONFIG_VERSION) {
        fclose(f);
        return -1;
    }

    /* read payload */
    unsigned char payload[2048];
    int len = (int)fread(payload, 1, sizeof(payload), f);
    fclose(f);

    if (len < 4) return -1;

    /* decrypt */
    xor_encrypt(payload, len);

    /* parse */
    memset(cfg, 0, sizeof(*cfg));
    int off = 0;
    cfg->type = payload[off++];

    strncpy(cfg->model, (char *)payload + off, sizeof(cfg->model) - 1);
    off += (int)strlen(cfg->model) + 1;

    if (off < len) {
        strncpy(cfg->api_key, (char *)payload + off, sizeof(cfg->api_key) - 1);
        off += (int)strlen(cfg->api_key) + 1;
    }

    if (off < len) {
        strncpy(cfg->base_url, (char *)payload + off, sizeof(cfg->base_url) - 1);
    }

    return 0;
}

/* ── Delete config ────────────────────────────────────────────────────── */

static void config_delete(void)
{
    char path[512];
    config_path(path, sizeof(path));
    unlink(path);
}

#endif /* CONFIG_H */

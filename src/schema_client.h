#ifndef SCHEMA_CLIENT_H
#define SCHEMA_CLIENT_H

#include "nl_service.h"

/*
 * Connect to EvoSQL via EVO text protocol (TCP).
 * Returns socket fd on success, -1 on failure.
 */
int evo_connect(const char *host, int port, const char *user, const char *password);

/*
 * Send a SQL statement over EVO protocol, collect response.
 * Returns heap-allocated response string (caller frees), NULL on error.
 */
char *evo_query(int sock, const char *sql);

/*
 * Close EVO connection.
 */
void evo_disconnect(int sock);

/*
 * Fetch full schema from EvoSQL (SHOW TABLES + column info per table).
 * Writes formatted schema text into session->schema_text.
 * Returns 0 on success, -1 on failure.
 */
int schema_fetch(NLSession *session, const NLConfig *cfg);

#endif /* SCHEMA_CLIENT_H */

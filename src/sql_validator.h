#ifndef SQL_VALIDATOR_H
#define SQL_VALIDATOR_H

/*
 * Validate that a SQL statement is in the allowed whitelist.
 *
 * Whitelist:
 *   CREATE TABLE, INSERT, SELECT, UPDATE, DELETE,
 *   ALTER TABLE, CREATE INDEX, DROP TABLE, DROP INDEX
 *
 * Blacklist (returns 0):
 *   DROP DATABASE, DROP SCHEMA, GRANT, REVOKE, CREATE USER,
 *   DROP USER, ALTER USER
 *
 * Returns 1 if allowed, 0 if blocked.
 */
int sql_validate(const char *sql);

/*
 * Sanitize SQL: trim whitespace, ensure single statement (no ';' followed
 * by more SQL), strip trailing semicolons.
 * Modifies the string in place, returns pointer to start.
 */
char *sql_sanitize(char *sql);

#endif /* SQL_VALIDATOR_H */

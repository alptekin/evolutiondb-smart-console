# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

EvoSQL Smart Console — NL→SQL service with multi-provider LLM support (Ollama, Claude, OpenAI, Groq, custom endpoints). Converts natural language to SQL and executes on EvoSQL via the EVO text protocol (TCP:9967).

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Dependencies: libcurl, libreadline, pthread.

Two build targets:
- **`nl-service`** — the TCP server (all `src/*.c` files)
- **`nl-client`** — the interactive CLI (`cli/nl-client.c` only)

Docker build: `docker compose up -d` (multi-stage: gcc:13 builder → debian:bookworm-slim runtime).

## Running

```bash
# Start NL service (defaults to Ollama on localhost:11434)
./build/nl-service

# Start interactive client
./build/nl-client
```

## Architecture

### Provider Abstraction

The core design pattern is a function-pointer interface in `src/provider.h`. Each provider (`provider_ollama.c`, `provider_claude.c`, `provider_openai.c`) exports a `Provider` struct with a `chat()` function pointer. Provider dispatch happens in `nl_service.c` via `provider_get(type)`.

Key difference between providers: Claude separates the system message into a top-level `system` field, while Ollama/OpenAI include it in the messages array.

Default models: Ollama→`qwen2.5:7b`, Claude→`claude-sonnet-4-20250514`, OpenAI→`gpt-4o-mini`.

### Request Flow

1. Client connects to TCP:9970 (`main.c` spawns a thread per connection)
2. `NL <text>` command → `nl_service.c` builds prompt with schema context + conversation history
3. Provider `chat()` called → HTTP POST to LLM API via `http_client.c`
4. SQL extracted from response, validated by `sql_validator.c` (whitelist: SELECT/INSERT/UPDATE/DELETE/CREATE/ALTER/INDEX; blacklist: DROP DATABASE/GRANT/CREATE USER)
5. `EXECUTE` → `schema_client.c` forwards SQL to EvoSQL via EVO text protocol (TCP:9967)

### JSON Handling

No external JSON library — `http_client.c` uses custom regex-based extraction (`json_extract_string`). When modifying provider response parsing, work within this pattern.

### CLI Client

`cli/nl-client.c` is a large standalone file (~66KB) with built-in i18n (`cli/i18n.h` — English/Turkish) and encrypted persistent config (`cli/config.h` — XOR with machine-derived key at `~/.evosql/nl-config.bin`).

## Protocol (TCP:9970)

```
NL <text>                              → QUESTION/SQL_PROPOSAL/ERROR
EXECUTE                                → RESULT <output>
REJECT                                 → OK
SCHEMA                                 → SCHEMA_OK <json>
DATABASE <name>                        → OK
PROVIDER <type> [model] [key] [url]    → OK
SQL <statement>                        → SQL_PROPOSAL <sql>
QUIT                                   → BYE
```

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| EVOSQL_NL_PORT | 9970 | Service port |
| EVOSQL_NL_PROVIDER | ollama | Default provider (ollama/claude/openai) |
| EVOSQL_NL_MODEL | (per provider) | Model name |
| EVOSQL_NL_API_KEY | (none) | API key for cloud providers |
| EVOSQL_NL_BASE_URL | (per provider) | Custom API endpoint |
| OLLAMA_HOST | http://localhost:11434 | Ollama API URL |
| EVOSQL_HOST | 127.0.0.1 | EvoSQL host |
| EVOSQL_PORT | 9967 | EvoSQL EVO port |
| EVOSQL_USER | admin | EvoSQL user |
| EVOSQL_PASSWORD | admin | EvoSQL password |

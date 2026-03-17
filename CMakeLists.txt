cmake_minimum_required(VERSION 3.14)
project(evolutiondb-smart-console C)

set(CMAKE_C_STANDARD 11)

# ── libcurl (for HTTP provider calls) ──────────────────────────────────
find_package(CURL REQUIRED)

# ── NL→SQL service ─────────────────────────────────────────────────────
add_executable(nl-service
    src/main.c
    src/nl_service.c
    src/schema_client.c
    src/sql_validator.c
    src/http_client.c
    src/provider_ollama.c
    src/provider_claude.c
    src/provider_openai.c
)

target_include_directories(nl-service PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CURL_INCLUDE_DIRS}
)

target_link_libraries(nl-service PRIVATE ${CURL_LIBRARIES} pthread)

# ── CLI client ─────────────────────────────────────────────────────────
add_executable(nl-client cli/nl-client.c)
target_include_directories(nl-client PRIVATE ${CMAKE_SOURCE_DIR}/cli)
target_link_libraries(nl-client PRIVATE readline)

# ── Install ────────────────────────────────────────────────────────────
install(TARGETS nl-service nl-client DESTINATION bin)

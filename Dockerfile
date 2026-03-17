FROM gcc:13 AS builder

RUN apt-get update && apt-get install -y cmake libcurl4-openssl-dev && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy source
COPY CMakeLists.txt .
COPY src/ src/
COPY cli/ cli/

# Build
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)

# ── Runtime ──────────────────────────────────────────────────────────────
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl4 libreadline8 locales ca-certificates \
    && sed -i 's/# tr_TR.UTF-8/tr_TR.UTF-8/' /etc/locale.gen \
    && sed -i 's/# en_US.UTF-8/en_US.UTF-8/' /etc/locale.gen \
    && locale-gen \
    && rm -rf /var/lib/apt/lists/*

ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

# Binaries
COPY --from=builder /build/build/nl-service /usr/local/bin/nl-service
COPY --from=builder /build/build/nl-client /usr/local/bin/nl-client

ENV EVOSQL_NL_PORT=9970
ENV EVOSQL_NL_PROVIDER=ollama
ENV OLLAMA_HOST=http://host.docker.internal:11434
ENV EVOSQL_HOST=evosql
ENV EVOSQL_PORT=9967
ENV EVOSQL_USER=admin
ENV EVOSQL_PASSWORD=admin

EXPOSE 9970

CMD ["nl-service"]

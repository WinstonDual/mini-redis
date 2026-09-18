# ============================================================
# Stage 1: builder
# ============================================================
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Сначала только CMakeLists — это кэширует слой с конфигурацией
COPY CMakeLists.txt ./
COPY src/ ./src/

RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build \
    && ./build/mini-redis-tests   # прогоняем тесты прямо в образе

# ============================================================
# Stage 2: runtime
# ============================================================
FROM debian:bookworm-slim AS runtime

# libstdc++ нужен для C++ runtime — в bookworm-slim он уже есть,
# но на всякий случай явно указываем обновить
RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# Не запускаться от root
RUN useradd -r -s /usr/sbin/nologin -u 1000 miniredis

WORKDIR /app
COPY --from=builder /src/build/mini-redis /app/mini-redis

USER miniredis

EXPOSE 6379
ENTRYPOINT ["/app/mini-redis"]
CMD ["6379"]
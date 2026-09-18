# mini-redis

A Redis-compatible in-memory key-value server written in modern C++20.

## Status

🚧 **Step 1** — TCP server skeleton. Responds `+PONG` on any connection.

## Requirements

- C++20 compiler (GCC 13+, Clang 16+, MSVC 19.30+)
- CMake 3.16+
- Ninja (or Make)

## Build

```bash
cmake -B build -G Ninja
cmake --build build
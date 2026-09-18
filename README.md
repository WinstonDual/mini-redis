# mini-redis

[![CI](https://github.com/WinstonDual/mini-redis/actions/workflows/ci.yml/badge.svg)](https://github.com/WinstonDual/mini-redis/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

A Redis-compatible in-memory key-value server written in modern **C++20**.

Built from scratch: custom RESP protocol parser, thread-safe store, TTL with lazy
expiration, and three data types (strings, lists, hashes).

## Features

- **Custom RESP parser** — incremental, byte-stream safe, zero-copy where possible
- **Thread-per-client** concurrency model with a thread-safe store
- **Three data types**: strings, lists, hashes
- **TTL** for any key (`EXPIRE` / `TTL` / `PERSIST` / `SET ... EX`)
- **Lazy + sweep expiration** — expired keys are removed on access or by a background sweep
- **WRONGTYPE** errors, just like the real Redis
- **100 unit tests** — no external frameworks, just a tiny macro-based harness
- **Cross-platform** — Windows (Winsock2) and Linux/macOS (POSIX sockets)
- **CI** — builds and tests on Ubuntu and Windows via GitHub Actions

## Supported commands

| Category   | Commands |
|------------|----------|
| Connection | `PING`, `ECHO`, `COMMAND` |
| Strings    | `SET`, `GET`, `DEL`, `EXISTS`, `TYPE`, `KEYS`, `DBSIZE` |
| TTL        | `EXPIRE`, `TTL`, `PERSIST`, `SET ... EX seconds` |
| Lists      | `LPUSH`, `RPUSH`, `LRANGE`, `LLEN`, `LPOP`, `RPOP`, `LINDEX` |
| Hashes     | `HSET`, `HGET`, `HDEL`, `HEXISTS`, `HLEN`, `HGETALL`, `HKEYS`, `HVALS` |

## Architecture

```
          ┌─────────────────┐
          │  redis-cli /    │
          │  any RESP client│
          └────────┬────────┘
                   │ TCP + RESP
                   ▼
     ┌─────────────────────────────┐
     │        TcpServer            │
     │   accept() → new thread     │
     └─────────────┬───────────────┘
                   │ per-client
                   ▼
     ┌─────────────────────────────┐
     │   RespParser  →  RespValue  │
     │   CommandDispatcher         │
     │           │                 │
     │           ▼                 │
     │      Store  (shared_mutex)  │
     │   string / list / hash      │
     │   + optional TTL            │
     └─────────────────────────────┘
```

## Build

### Prerequisites

- C++20 compiler (GCC 13+, Clang 16+, MSVC 19.30+)
- CMake 3.16+
- Ninja (or any CMake generator)

### Windows (MSYS2 / MinGW)

```powershell
cmake -B build -G Ninja
cmake --build build
```

### Linux / macOS

```bash
sudo apt-get install -y cmake ninja-build g++  # Debian/Ubuntu
cmake -B build -G Ninja
cmake --build build
```

## Run

```bash
./build/mini-redis            # listens on port 6379
./build/mini-redis 7000       # custom port
```

## Test

```bash
./build/mini-redis-tests
```

Or via CTest:

```bash
cd build && ctest --output-on-failure
```

## Quick start

In one terminal:

```bash
./build/mini-redis
```

In another (using a real `redis-cli`, if installed):

```bash
redis-cli -p 6379
127.0.0.1:6379> SET greeting "hello"
OK
127.0.0.1:6379> GET greeting
"hello"
127.0.0.1:6379> RPUSH fruits apple banana cherry
(integer) 3
127.0.0.1:6379> LRANGE fruits 0 -1
1) "apple"
2) "banana"
3) "cherry"
127.0.0.1:6379> HSET user:1 name Alice age 30
(integer) 2
127.0.0.1:6379> HGETALL user:1
1) "name"
2) "Alice"
3) "age"
4) "30"
127.0.0.1:6379> SET session:abc token123 EX 60
OK
127.0.0.1:6379> TTL session:abc
(integer) 60
```

If `redis-cli` is not installed, you can test with any TCP client that speaks RESP
(see the `Test` section above — the tests exercise the parser and dispatcher directly).

## Project layout

```
mini-redis/
├── CMakeLists.txt
├── Dockerfile
├── README.md
├── LICENSE
├── src/
│   ├── main.cpp
│   ├── net/           # sockets, TCP server, per-client loop
│   ├── resp/          # RESP value, parser, serializer
│   ├── server/        # Store, command dispatcher
│   └── tests/         # unit tests, mini test framework
└── .github/workflows/ # CI
```

## Roadmap

- [x] Step 1 — TCP server skeleton
- [x] Step 2 — RESP parser + serializer
- [x] Step 3 — Command dispatcher (PING / ECHO / COMMAND)
- [x] Step 4 — Thread-per-client concurrency
- [x] Step 5 — Store with GET / SET / DEL / EXISTS / KEYS / TYPE / DBSIZE
- [x] Step 6 — TTL / EXPIRE / PERSIST
- [x] Step 7 — Lists (LPUSH / RPUSH / LRANGE / LLEN / LPOP / RPOP / LINDEX)
- [x] Step 8 — Hashes (HSET / HGET / HDEL / HEXISTS / HLEN / HGETALL / HKEYS / HVALS)
- [x] Step 9 — CI, README, Docker, release v0.1.0

### Future

- [ ] Event loop (epoll / IOCP) instead of thread-per-client
- [ ] Pub/Sub (`SUBSCRIBE` / `PUBLISH`)
- [ ] Transactions (`MULTI` / `EXEC`)
- [ ] RDB snapshots / AOF persistence
- [ ] `SCAN`, `SETEX`, `GETSET`, `APPEND`, `INCR` / `DECR`
- [ ] Sets and sorted sets

## License

MIT — see [LICENSE](LICENSE).
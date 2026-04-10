# ServerCore v4

High-performance C++23 game server framework built on Linux `io_uring`.

A modular, zero-copy networking library that provides async I/O primitives — without forcing any event-loop pattern, protocol framing, or room/session model. Game servers, HTTP servers, and custom TCP services are all built as applications on top of the same core.

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│   GameServer · WebServer · BattleServer · EchoServer · ...   │
├──────────────────────────────────────────────────────────────┤
│                   Framework Extensions                       │
│   ServerWeb          ServerGame           ServerStorage      │
│   (HTTP/WS)        (Match/Presence)      (PostgreSQL)        │
├──────────────────────────────────────────────────────────────┤
│                     ServerCore (Base)                        │
│  ┌──────────┐  ┌──────────┐  ┌─────────┐  ┌─────────────┐  │
│  │  IoRing   │  │ Session  │  │ Buffers │  │  Job System │  │
│  │(io_uring) │  │ Listener │  │  Pools  │  │  + Timers   │  │
│  └──────────┘  └──────────┘  └─────────┘  └─────────────┘  │
├──────────────────────────────────────────────────────────────┤
│  liburing · spdlog · GLM · llhttp · OpenSSL · libpq         │
└──────────────────────────────────────────────────────────────┘
```

## Modules

### ServerCore — Async I/O Primitives

The foundation layer. No protocol assumptions, no game logic.

| Component | Description |
|-----------|-------------|
| **IoRing** | io_uring wrapper — `Dispatch()` event loop, SQE builders for accept/recv/send/connect, `RunOnRing()` cross-thread posting |
| **Session** | TCP connection with `pending_io_` lifetime management, `OnRecv(span<byte>)` virtual, `Send(SendBufferRef)` |
| **Listener** | Multishot accept, SO_REUSEPORT for multi-worker binding |
| **BufferPool** | 4MB chunk-based send buffer pool, atomic ref-counting, zero-copy broadcast |
| **RingBuffer** | io_uring provided buffer ring for recv — eliminates per-connection buffer pre-allocation |
| **JobQueue** | Single-threaded serialized work queue, `Push()` from any thread, time-budgeted `Execute(deadline)` |
| **GlobalQueue** | Multi-queue scheduler — distributes execution across multiple JobQueues |
| **JobTimer** | Periodic/one-shot timers integrated with IoRing |
| **MpscQueue** | Lock-free multi-producer single-consumer queue |

### ServerWeb — HTTP/WebSocket Framework

Full-featured web framework built on ServerCore.

| Component | Description |
|-----------|-------------|
| **WebServer** | Multi-worker HTTP server — each worker owns its own IoRing + Listener |
| **Router** | Radix-tree based path matching with `:param` and wildcard support |
| **RouteGroup** | Composable sub-routers with shared middleware |
| **HttpParser** | Based on llhttp (Node.js HTTP parser) |
| **HttpSession** | HTTP connection state machine with WebSocket upgrade path |
| **Middleware** | Pipeline pattern — Logger, CORS, StaticFiles, Auth (JWT) built-in |
| **WebSocket** | Full WS support — frame parsing, handshake, text/binary/ping/pong |

### ServerGame — Match/Presence/Multiplayer

Game-specific framework layer (currently disabled in build — needs type definitions from consumer).

| Component | Description |
|-----------|-------------|
| **Match** | Game instance extending JobQueue — `RequestJoin`, `DispatchMessage`, `BroadcastAll` |
| **MatchHandler** | Pure virtual game logic interface — `OnJoin`, `OnLeave`, `OnMessage`, `OnTick` |
| **MatchRegistry** | Factory-creates and manages match instances |
| **MatchmakerQueue** | Queue-based matchmaking with ticket system and query parser |
| **PlayerRegistry** | Online player index — register/unregister/lookup |
| **PresenceTracker** | Player status tracking with event notifications |
| **NotificationService** | Pluggable notification delivery |

### ServerStorage — Async PostgreSQL

Dedicated worker-thread pool for blocking DB queries with callback delivery to IoRing threads.

| Component | Description |
|-----------|-------------|
| **PgWorkerPool** | N worker threads (1:1 with connections), `Submit(sql, params, callback)` |
| **PgConnection** | libpq connection wrapper with parameterized queries |
| **PgResult** | RAII result wrapper — row/column access, error handling |
| **PgConnectionPool** | Facade interface for `Initialize` / `Execute` |
| **QueryBuilder** | SQL construction utilities |

## Examples

| Example | Port | Description |
|---------|------|-------------|
| **EchoServer** | 9100 | Minimal packet echo — demonstrates Session subclassing and binary framing |
| **WebServer** | 8080 | REST API + SPA — Router, Middleware pipeline, async DB queries |
| **GameServer** | 7777 | Multiplayer dungeon crawler — multi-worker I/O, room simulation, bot AI (A\*), pluggable DB (Memory/SQLite/PostgreSQL) |
| **BattleServer** | — | Faction battle simulator — protobuf protocol, C_SCENE_READY flow control |
| **AntBattleServer** | — | Ant colony simulation server |
| **BoidServer** | — | Boid flocking simulation server |
| **DX12DemoServer** | — | DirectX 12 rendering demo server |

**Benchmarks:** `GameServerIntegrated` (single IoRing) vs `GameServerSeparated` (per-worker IoRing) vs `GameServerEpoll` (epoll baseline).

## Build

### Requirements

- Linux with io_uring support (kernel 5.19+)
- C++23 compiler (GCC 13+ or Clang 17+)
- CMake 3.16+
- System packages: `liburing-dev`, `libpq-dev`, `libssl-dev`

### Compile

```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `SERVERCORE_BUILD_TESTS` | `ON` | Build unit tests |
| `SERVERCORE_TRACY` | `OFF` | Enable Tracy profiler integration |
| `SERVERCORE_SANITIZER` | `off` | `tsan`, `asan`, `ubsan`, `asan+ubsan` |

```bash
# ThreadSanitizer build
cmake -DSERVERCORE_SANITIZER=tsan -B build-tsan
cmake --build build-tsan -j$(nproc)

# Tracy profiler build
cmake -DSERVERCORE_TRACY=ON -B build-tracy
cmake --build build-tracy -j$(nproc)
```

### Run Tests

```bash
cd build
ctest --output-on-failure
```

## Dependencies

Fetched automatically via CMake FetchContent:

| Library | Version | Purpose |
|---------|---------|---------|
| [spdlog](https://github.com/gabime/spdlog) | 1.15.1 | Structured logging |
| [GLM](https://github.com/g-truc/glm) | 1.0.1 | Header-only math (vec3, mat4, etc.) |
| [llhttp](https://github.com/nodejs/llhttp) | 9.3.1 | HTTP parsing (ServerWeb) |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | JSON serialization (ServerWeb) |
| [Google Test](https://github.com/google/googletest) | 1.15.0 | Unit testing |
| [Tracy](https://github.com/wolfpld/tracy) | 0.13.1 | Profiler (optional) |

System packages:

| Library | Purpose |
|---------|---------|
| liburing | io_uring kernel interface |
| libpq | PostgreSQL client (ServerStorage) |
| OpenSSL | TLS + crypto (ServerWeb JWT/Auth) |
| Protobuf | Protocol serialization (optional, for examples) |

## Design Principles

- **Ring-per-thread** — Each worker owns an independent IoRing. No global I/O lock contention. Cross-thread work posted via `RunOnRing()`.
- **Zero-copy where possible** — Provided buffer rings feed directly to parsers. SendBuffer reuses pooled chunks. Span-based APIs avoid copies.
- **`pending_io_` lifetime** — Sessions self-own via `shared_ptr`. Released only when all in-flight I/O completes. No use-after-free with async I/O.
- **Job serialization** — `JobQueue` guarantees single-threaded execution per queue. Multiple queues scheduled by `GlobalQueue` with time-budgeted yields.
- **Composition over inheritance** — Middleware pipelines compose handlers. RouteGroups assemble sub-routes. MatchRegistry factory-creates handlers.
- **`std::expected` error handling** — No exceptions in hot paths. RAII guards for all resources. `move_only_function` for zero-copy callbacks.
- **No framework lock-in** — Core imposes no event loop, protocol, or session model. Applications compose primitives freely.

## Protocol

Binary header: `[size:uint16][msgId:uint16]` + Protobuf 3 payload (for game examples).

WebSocket + JSON available via ServerWeb for web-facing services.

## Test Coverage

30 test files across all modules:

| Module | Tests |
|--------|-------|
| Core | MpscQueue, JobQueue, GlobalQueue, JobTimer |
| Ring | IoRing poll_add |
| I/O | Session lifecycle, cross-thread flow |
| Web | HTTP parser, Router, RadixTree, WebSocket (frame/handshake/integration), JWT, Middleware |
| Storage | PgResult |
| Game | PlayerRegistry, Match, MatchRegistry, MatchmakerQueue, QueryParser, PresenceTracker, NotificationService |
| Server | Game match integration, streaming, collision, voxel world |
| Shared | Protocol, SDF, SVO, ByteBuffer |

## License

Private — All rights reserved.

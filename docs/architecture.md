# Architecture

This document records the design decisions behind the framework. It is written
for contributors and operators; the operational workflow is described in
`README.md`.

## Goals and priorities

1. **Operational and usage simplicity** (hard requirement).
2. Backend only; frontend is a separate concern.
3. Modern C++ (C++23, selected C++26 ideas) as a learning vehicle.
4. Friendly to AI coding agents (clear boundaries, one command per task).

## Layering

```
apps/dashboard ─┐
                ├─► framework/server ─► framework/http ─► framework/net
framework/plugin┘                                             │
                                                              ▼
                                        framework/execution ─► framework/core
```

Dependency direction: applications depend on the framework, never the reverse.
`execution` and `core` are leaf modules.

## The execution layer

The execution model is the sender/receiver model of `std::execution` (P2300),
using NVIDIA's reference implementation, *plus* C++20 coroutines. The two are
not separate worlds:

- `stdexec::task` is both a coroutine return type and a sender.
- A sender can be `co_await`-ed inside a task (single value completion shape).
- Any awaitable can be used as a sender.

`framework/execution` and `framework/net` are the only layers permitted to
expose third-party types. `execution` publishes `fw::task`, `fw::sync_wait`,
`fw::just`, `fw::then`, `fw::let_value`, `fw::starts_on`; `net` publishes
`fw::net::io_context` and async I/O senders. This exception is deliberate: these
layers *are* the executor and I/O abstraction.

Cancellation uses stop tokens carried in the task environment; a request that
is cancelled (client disconnect, timeout) stops downstream work.

## The I/O layer

`framework/net` runs work on Asio and bridges Asio's completion handlers into
senders by way of stdexec's `<exec/asio/...>` integration:

- `fw::net::io_context` owns an Asio thread pool and hands out a P2300
  `scheduler`, so pipelines can run on I/O threads via `fw::starts_on`.
- `fw::net::async_sleep` (and future socket operations) return senders that can
  be `co_await`-ed inside `fw::task`, keeping one async model end to end.
- stdexec's integration needs a generated `exec/asio/asio_config.hpp`; the
  standalone-Asio variant lives in `framework/net/compat/` so the vendored
  submodules stay untouched.

## The HTTP layer

`framework/http` owns request parsing, request/response models and response
serialization; it exposes no parser types.

- `fw::http::parse_request(buffer, request)` returns `complete`, `incomplete`
  (read more and retry) or `error`, and reports how many bytes were consumed.
  It handles the request line, headers and a `Content-Length` body; chunked
  transfer encoding is rejected for now.
- `fw::http::Request` owns the method, target, path/query split, headers and
  body, with a case-insensitive `header(name)` lookup.
- `fw::http::Response` has a status, headers and body, plus `text(...)` /
  `json(...)` helpers and `serialize()`.

`framework/server` provides `fw::server::Router` and `fw::server::Server`.

- `Router`: compiles route patterns into an immutable segment tree held behind
  a `std::atomic<std::shared_ptr<const RouteTable>>`. `rebuild()` swaps the table
  atomically (hot reload) and `dispatch` reads it lock-free. Patterns support
  literal segments, `:name`/`{name}` parameters, `{name:constraint}` (built-in
  `int`/`uuid`/`slug`), `{name:*}` catch-all and `{name?}` optional segments.
  Matching prefers literals, backtracks to parameters, and returns `404`, or
  `405` with `Allow` (and automatic `OPTIONS`/`HEAD`-to-`GET` fallback).
  `TrailingSlash` and case-insensitive policies are configurable, and captured
  parameters are percent-decoded and written into stack storage.
- Handlers return `HandlerResult`, a ready `Response` (zero-allocation fast path)
  or an `fw::task<http::Response>`; a lambda returning either converts
  implicitly, and `sync_handler`/`async_handler` are available for explicitness.
  Middleware composes handlers (use `as_task` to await the next handler), and
  `RouteGroup` shares a prefix and a middleware stack.
- `Server`: binds a `fw::net::tcp_listener` and runs an accept loop. Each
  connection is handled by a detached coroutine that reads into a buffer,
  parses, dispatches and writes the response. It supports HTTP/1.1 keep-alive, a
  per-connection idle timeout, a keep-alive request cap, `HEAD` responses without
  a body, `400` on malformed requests and `413` beyond `max_request_bytes`.
  `stop()` stops accepting, finishes the main loop and lets in-flight
  connections drain; `wait()` blocks until they have.
  `start()` is **non-blocking** (worker threads only) — the escape hatch that
  keeps the calling thread in control. `run()` blocks driving the server's own
  `fw::run_loop` (exposed via `main_scheduler()`, so workers can hand work to the
  main thread) until `stop()`. Signals are deliberately **out of scope** (they
  are process-level): the application installs them, e.g. via
  `fw::net::on_signal(io, {SIGINT, SIGTERM}, [&] { server.stop(); })`.

## Dependency policy

- The dependency manifest is `xmake.lua`; `CMakeLists.txt` mirrors it.
- Prefer libraries already present in the environment.
- Public headers under `framework/*/include/` must not expose third-party types
  (except `execution`, as noted above).
- `stdexec`, `toml++`, `asio`, `picohttpparser` and `unordered_dense` are git
  submodules under `third_party/`, pinned to exact commits. PCRE2 (`pcre2-8`)
  and the other system libraries come from the distribution. Run
  `git submodule update --init --recursive` after cloning; a local HTTP/SOCKS
  proxy may be needed to fetch GitHub.

Chosen dependencies (phase 0):

| Concern | Library | Notes |
|---|---|---|
| Execution | stdexec (submodule, pinned) | via `fw::` aliases only |
| I/O | Asio (submodule, pinned) | via `fw::net`, bridged with `<exec/asio>` |
| HTTP parsing | picohttpparser (submodule, pinned) | wrapped by `fw::http`; chosen over llhttp, whose C is generated (needs `llparse`) |
| Routing | own (`fw::server::Router`) | `:name` / `{name}` / `{name:constraint}` / `{name:*}` / `{name?}` |
| Logging | spdlog | wrapped by `fw::log` |
| Configuration | toml++ (submodule) | wrapped by `fw::Config`, pimpl |
| Testing | GoogleTest | tests only |

Planned later: Asio (I/O), llhttp/nghttp2 (HTTP), simdjson (JSON), SQLite.

## Configuration

`fw::Config` loads TOML and answers dotted-path queries. Overrides follow the
order **defaults < file < environment**. Environment variables are derived from
the key by upper-casing and replacing `.`/`-` with `_`, optionally prefixed:

```
[server]
port = 8080      # FW_SERVER_PORT overrides (prefix "FW_")
```

The configuration center and remote/bootstrap sources are a later phase; local
files are self-sufficient and must remain so.

For type-safe access, declare a view struct and a `fw::config_schema<T>`
specialization that lists fields with their key, member and default:

```cpp
struct server_settings { std::string host; std::uint16_t port; };

template <>
struct fw::config_schema<server_settings> {
    static constexpr auto fields() {
        return std::tuple{
            fw::field{"server.host", &server_settings::host, std::string{"0.0.0.0"}},
            fw::field{"server.port", &server_settings::port, std::uint16_t{8080}},
        };
    }
};

auto settings = config.bind<server_settings>();   // Result<server_settings>
```

The member pointers give compile-time checking of each field's type, integer
fields are range-checked (e.g. a port above 65535 is rejected), and a key
present with the wrong type returns an error. Reflection, when available, can
generate these schemas automatically later; no reflection is required now.

## Directory layout

```
framework/core        errors (fw::Result/fw::Error), logging, configuration
framework/execution   fw::task and sender aliases (stdexec-facing)
framework/net         Asio thread pool and async I/O senders (exec/asio-facing)
framework/http        HTTP/1.1 parsing (picohttpparser), request/response types
framework/server      routing (fw::server::Router) and, later, the connection loop
apps/dashboard        composition root and entry point
third_party/          git submodules (pinned)
tests/                one directory per module
```

## Roadmap

- **Phase 0 (current)** — skeleton: core, execution, I/O foundation, HTTP
  parsing, routing, a minimal HTTP/1.1 server, dual build, tests.
- **Phase 1** — vertical slice: SQLite repository, richer HTTP features
  (coroutine handlers, SSE, timeouts), `deployctl`
  (init/validate/doctor/run/migrate), health/metrics endpoints, service
  packaging.
- **Phase 2** — dashboard domain: OpenAPI contract, auth, contests/standings.
- **Phase 3** — configuration center (`configd`) with bootstrap and degradation.
- **Phase 4** — plugin host: C ABI, Lua hot-loading, Python sidecar.
- **Phase 5** — multi-instance deployment, observability.

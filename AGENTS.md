# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project

C++23 backend framework and dashboard service. See `README.md` and
`docs/architecture.md`. The current milestone is **Phase 0** (skeleton); do not
add HTTP/storage/plugin code ahead of the roadmap unless asked.

## Hard rules

- **No comments in code** unless explaining a genuinely non-obvious decision.
- **No third-party types in public headers** under `framework/*/include/`, with
  the exception of `framework/execution/`, `framework/net/` and the
  `framework/server/` router surface, which expose `stdexec`/Asio types behind
  `fw::` names.
- Business/HTTP/plugin interfaces must not leak library types either.
- Public headers live in `include/<module>/`, implementations in `src/`.
- Match the existing style; run the formatter before finishing.

## Commands

Build and test with **xmake** (primary):

```sh
xmake f -m debug          # configure (debug)
xmake build               # build all targets
xmake run dashboard       # run the dashboard binary
xmake test                # run the test suite
```

Compatibility build with **CMake** (uses `build-cmake/`, since xmake owns
`build/`):

```sh
cmake -S . -B build-cmake -G Ninja
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
```

Format / lint:

```sh
clang-format -i framework/**/*.hpp framework/**/*.cpp apps/**/*.cpp tests/**/*.cpp
clang-tidy -p . framework/**/*.cpp
```

## Keeping the two builds in sync

`xmake.lua` and `CMakeLists.txt` must stay equivalent. Both glob their sources
(`src/*.cpp`, `tests/**/*.cpp`), so adding files needs no build edits, but any
change to targets, options or dependencies must be applied to **both**.

Option mapping:

| xmake | CMake |
|---|---|
| `tests` | `FW_BUILD_TESTS` |
| `metrics` | `FW_METRICS` |
| `reflection` | `FW_REFLECTION` |

## IDE support

For clangd (recommended in VSCode, via the clangd extension), generate a
compilation database from the primary build:

```sh
xmake f -m debug
xmake project -k compile_commands
```

This writes `compile_commands.json` to the repository root, which `.clangd`
picks up. CMake users may instead configure with CMake and point `.clangd` at
`build-cmake`.

## Layout

- `framework/core` — `fw::Result`/`fw::Error`, `fw::log`, `fw::Config`.
- `framework/execution` — `fw::task`, `fw::sync_wait`; stdexec-facing.
- `framework/net` — `fw::net::io_context` and async I/O senders; Asio-facing,
  with the stdexec `<exec/asio>` config shim in `framework/net/compat/`.
- `framework/http` — `fw::http` request/response types and parser
  (picohttpparser).
- `framework/server` — `fw::server::Router` and `fw::server::Server` (accept
  loop and connection handling).
- `apps/dashboard` — composition root and entry point.
- `tests/` — one directory per module.
- `third_party/stdexec`, `third_party/tomlplusplus`, `third_party/asio`,
  `third_party/picohttpparser` — git submodules, pinned to exact commits. Run
  `git submodule update --init --recursive` after cloning; update a submodule
  only deliberately and stage the new gitlink.

## Dependency policy

Prefer libraries already available in the environment. The dependency manifest
is `xmake.lua` (single source of truth); keep `CMakeLists.txt` in sync.
Third-party dependencies that are not packaged on the target systems
(`stdexec`, `toml++`, `asio`, `picohttpparser`, `unordered_dense`) are git
submodules under `third_party/`, pinned to exact commits. PCRE2 is expected as a
system library. A local HTTP/SOCKS proxy may be required to fetch GitHub.

# DMU/ICPC Dashboard — Backend Framework

Backend for an ACM/ICPC training-team information dashboard, and a learning
project for the team's development and operations group.

The project has two faces:

- **A reusable C++ framework** (`framework/`) built on C++23, C++20 coroutines
  and the sender/receiver model (`std::execution`, P2300).
- **Applications** (`apps/`) built on top of it, starting with the dashboard.

> Status: **Phase 0 — skeleton**. The framework core, execution layer and a
> minimal dashboard entry point build and are tested. HTTP, storage and the
> service itself arrive in later phases.

## Design principles

- **Operational simplicity first.** Single binary, minimal runtime dependencies,
  one local config file, easy to deploy and operate.
- **Interface isolation.** Third-party types do not appear in public headers,
  with one deliberate exception: the `execution/` layer *is* the execution
  abstraction, so it exposes `stdexec` behind `fw::` aliases.
- **Backend only.** The dashboard frontend is a separate concern; this repo
  serves REST/SSE.
- **Agent friendly.** Clear module boundaries, one command for each task.

## Layout

```
framework/
  core/        error handling, logging, configuration, composition conventions
  execution/   fw::task + scheduler concepts (the only stdexec-facing layer)
  net/         Asio thread pool and async I/O senders (exec/asio-facing)
  http/        HTTP/1.1 parsing, request/response types and an async client
  server/      routing and the HTTP/1.1 server (accept loop + connections)
apps/
  dashboard/   the service entry point
third_party/
  stdexec/     git submodules, pinned
  tomlplusplus/
  asio/
  picohttpparser/
tests/         unit tests (GoogleTest)
docs/          architecture notes
```

## Building

This repository uses git submodules for header-only dependencies. After
cloning:

```sh
git clone --recurse-submodules <url>
# or, for an existing checkout:
git submodule update --init --recursive
```

A local HTTP/SOCKS proxy may be required to fetch GitHub.

You need a C++23 compiler (GCC 14+ or Clang 18+ recommended). System
dependencies:

- `spdlog` and `fmt`
- `zlib` (gzip response decoding)
- `PCRE2` (route constraint regexes)
- `GoogleTest` (tests only)

On Arch Linux:

```sh
sudo pacman -S --needed spdlog fmt zlib pcre2 gtest
```

`stdexec`, `toml++`, `asio` and `picohttpparser` are git submodules under
`third_party/`, pinned to exact commits.

### xmake (primary)

```sh
xmake f -m release
xmake build
xmake run dashboard
xmake test
```

### CMake (compatibility)

```sh
cmake -S . -B build-cmake -G Ninja
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
```

### Editor / IDE

The xmake build is primary; generate a compilation database for clangd (the
recommended VSCode setup):

```sh
xmake f -m debug
xmake project -k compile_commands   # writes compile_commands.json to the root
```

`.clangd` picks it up automatically. CMake Tools users can build with CMake
instead; the database lands in `build-cmake/`.

## License

Apache-2.0. See `LICENSE`. Third-party submodules keep their own licenses (see
`third_party/*/LICENSE*`).

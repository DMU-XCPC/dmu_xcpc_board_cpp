#pragma once

#include "fw/execution/run_loop.hpp"
#include "fw/net/io.hpp"
#include "fw/net/tcp.hpp"
#include "fw/server/router.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace fw::server {

struct ServerOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{8080};
    std::size_t max_request_bytes{1U << 20U};
    std::size_t max_keep_alive_requests{1000};
    std::chrono::milliseconds idle_timeout{30000};
};

// A minimal HTTP/1.1 server: accepts connections on the I/O context, parses
// requests with `fw::http` and dispatches them through the router. Keep-alive
// is supported; chunked transfer encoding is not (yet).
//
// The server must outlive the I/O context.
//
// Lifecycle:
//   * `start()` only launches worker threads and returns immediately — the
//     escape hatch that leaves the calling thread in control.
//   * `run()` blocks the calling thread driving the server's main-thread
//     `fw::run_loop` until `stop()` is called.
//   * `stop()` stops accepting, finishes the main loop and lets in-flight
//     connections drain; `wait()` blocks until they have.
//
// Worker threads can hand work to the main thread through `main_scheduler()`.
// Signals are intentionally out of scope: install your own and call `stop()`.
class Server {
public:
    Server(fw::net::io_context& context, ServerOptions options = {});
    ~Server();

    Server(Server const&) = delete;
    Server& operator=(Server const&) = delete;

    [[nodiscard]] Router& router() noexcept {
        return router_;
    }
    [[nodiscard]] std::uint16_t port() const;
    [[nodiscard]] std::size_t active_connections() const noexcept {
        return active_.load();
    }
    [[nodiscard]] auto main_scheduler() noexcept {
        return main_loop_.get_scheduler();
    }

    void start();
    void run();
    void stop();
    void wait();

private:
    fw::task<void> accept_loop();
    fw::task<void> handle(fw::net::tcp_socket socket);

    fw::net::io_context& context_;
    ServerOptions options_;
    Router router_;
    std::unique_ptr<fw::net::tcp_listener> listener_;
    fw::run_loop main_loop_;

    std::atomic<std::size_t> active_{0};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::condition_variable drained_;
};

} // namespace fw::server

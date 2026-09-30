#pragma once

#include "fw/net/io.hpp"
#include "fw/net/tcp.hpp"
#include "fw/server/router.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace fw::server {

struct ServerOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{8080};
    std::size_t max_request_bytes{1U << 20U};
};

// A minimal HTTP/1.1 server: accepts connections on the I/O context, parses
// requests with `fw::http` and dispatches them through the router. Keep-alive
// is supported; chunked transfer encoding is not (yet).
//
// The server must outlive the I/O context. Call `stop()` to stop accepting new
// connections; in-flight connections finish on their own.
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

    void start();
    void stop();

private:
    fw::task<void> accept_loop();
    fw::task<void> handle(fw::net::tcp_socket socket);

    fw::net::io_context& context_;
    ServerOptions options_;
    Router router_;
    std::unique_ptr<fw::net::tcp_listener> listener_;
};

} // namespace fw::server

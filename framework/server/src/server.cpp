#include "fw/server/server.hpp"

#include "fw/http/parser.hpp"
#include "fw/http/response.hpp"

#include <exec/start_detached.hpp>

#include <array>
#include <exception>
#include <string>
#include <utility>

namespace fw::server {
namespace {

bool wants_close(fw::http::Request const& request) {
    auto const connection = request.header("connection");
    if (connection.has_value() && fw::http::iequals(*connection, "close")) {
        return true;
    }
    return request.minor_version < 1;
}

void detach(fw::net::io_context& context, fw::task<void> task) {
    exec::start_detached(fw::starts_on(context.get_scheduler(), std::move(task)) |
                         fw::upon_error([](std::exception_ptr const&) noexcept {}));
}

} // namespace

Server::Server(fw::net::io_context& context, ServerOptions options)
    : context_(context), options_(std::move(options)),
      listener_(std::make_unique<fw::net::tcp_listener>(context_, options_.host, options_.port)) {}

Server::~Server() = default;

std::uint16_t Server::port() const {
    return listener_ ? listener_->port() : 0;
}

void Server::start() {
    if (listener_) {
        detach(context_, accept_loop());
    }
}

void Server::stop() {
    if (listener_) {
        listener_->close();
    }
}

fw::task<void> Server::accept_loop() {
    while (listener_) {
        auto socket = co_await listener_->async_accept();
        detach(context_, handle(std::move(socket)));
    }
    co_return;
}

fw::task<void> Server::handle(fw::net::tcp_socket socket) {
    std::string buffer;
    std::array<char, 8192> chunk{};

    while (true) {
        fw::http::Request request;
        auto const parsed = fw::http::parse_request(buffer, request);

        if (parsed.state == fw::http::ParseState::error) {
            auto const response = fw::http::Response::text(400, "bad request\n");
            auto const out = response.serialize();
            co_await socket.write(out);
            break;
        }

        if (parsed.state == fw::http::ParseState::incomplete) {
            std::size_t const received = co_await socket.read_some(chunk);
            if (received == 0) {
                break;
            }
            buffer.append(chunk.data(), received);
            if (buffer.size() > options_.max_request_bytes) {
                auto const response = fw::http::Response::text(413, "request too large\n");
                auto const out = response.serialize();
                co_await socket.write(out);
                break;
            }
            continue;
        }

        buffer.erase(0, parsed.consumed);

        auto response = router_.dispatch(request);
        bool const keep_alive = !wants_close(request);
        if (!keep_alive) {
            response.set_header("Connection", "close");
        }
        auto const out = response.serialize();
        co_await socket.write(out);
        if (!keep_alive) {
            break;
        }
    }

    socket.close();
    co_return;
}

} // namespace fw::server

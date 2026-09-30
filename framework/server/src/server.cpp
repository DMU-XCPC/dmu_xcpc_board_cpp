#include "fw/server/server.hpp"

#include "fw/http/parser.hpp"
#include "fw/http/response.hpp"

#include <asio.hpp>
#include <exec/start_detached.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
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

struct ActiveGuard {
    std::atomic<std::size_t>* active;
    std::mutex* mutex;
    std::condition_variable* drained;

    ActiveGuard(std::atomic<std::size_t>& counter, std::mutex& lock,
                std::condition_variable& signal)
        : active(&counter), mutex(&lock), drained(&signal) {}

    ActiveGuard(ActiveGuard const&) = delete;
    ActiveGuard& operator=(ActiveGuard const&) = delete;

    ~ActiveGuard() {
        if (active->fetch_sub(1) == 1) {
            std::scoped_lock lock{*mutex};
            drained->notify_all();
        }
    }
};

// Reads into `buffer`, cancelling the socket (and thus closing the connection)
// when `timeout` elapses first.
fw::task<std::size_t> read_with_timeout(std::shared_ptr<fw::net::tcp_socket> connection,
                                        std::shared_ptr<asio::steady_timer> timer,
                                        std::span<char> buffer, std::chrono::milliseconds timeout) {
    std::weak_ptr<fw::net::tcp_socket> weak = connection;
    timer->expires_after(timeout);
    timer->async_wait([weak](std::error_code error) {
        if (!error) {
            if (auto sock = weak.lock()) {
                std::error_code ignored;
                [[maybe_unused]] auto const cancelled = sock->native().cancel(ignored);
            }
        }
    });
    std::size_t const received = co_await connection->read_some(buffer);
    timer->cancel();
    co_return received;
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
    if (!listener_) {
        return;
    }
    stopping_.store(false);
    [[maybe_unused]] auto const built = router_.rebuild();
    detach(context_, accept_loop());
}

void Server::run() {
    running_.store(true);
    if (stopping_.load()) {
        main_loop_.finish();
    }
    main_loop_.run();
    running_.store(false);
}

void Server::stop() {
    if (stopping_.exchange(true)) {
        return;
    }
    if (listener_) {
        listener_->close();
    }
    if (running_.load()) {
        main_loop_.finish();
    }
}

void Server::wait() {
    std::unique_lock lock{mutex_};
    drained_.wait(lock, [this] { return active_.load() == 0; });
}

fw::task<void> Server::accept_loop() {
    while (listener_) {
        auto socket = co_await listener_->async_accept();
        detach(context_, handle(std::move(socket)));
    }
    co_return;
}

fw::task<void> Server::handle(fw::net::tcp_socket socket) {
    active_.fetch_add(1);
    ActiveGuard guard{active_, mutex_, drained_};

    auto connection = std::make_shared<fw::net::tcp_socket>(std::move(socket));
    auto const executor = connection->native().get_executor();
    auto timer = std::make_shared<asio::steady_timer>(executor);

    std::string buffer;
    std::array<char, 8192> chunk{};
    std::size_t served = 0;

    while (true) {
        fw::http::Request request;
        auto const parsed = fw::http::parse_request(buffer, request);

        if (parsed.state == fw::http::ParseState::error) {
            auto const response = fw::http::Response::text(400, "bad request\n");
            auto const out = response.serialize();
            co_await connection->write(out);
            break;
        }

        if (parsed.state == fw::http::ParseState::incomplete) {
            std::size_t const received =
                co_await read_with_timeout(connection, timer, chunk, options_.idle_timeout);
            if (received == 0) {
                break;
            }
            buffer.append(chunk.data(), received);
            if (buffer.size() > options_.max_request_bytes) {
                auto const response = fw::http::Response::text(413, "request too large\n");
                auto const out = response.serialize();
                co_await connection->write(out);
                break;
            }
            continue;
        }

        buffer.erase(0, parsed.consumed);

        fw::http::Response response;
        try {
            response = co_await router_.dispatch(request);
        } catch (...) {
            response = fw::http::Response::text(500, "internal server error\n");
        }

        ++served;
        bool const keep_alive =
            !wants_close(request) && served < options_.max_keep_alive_requests && !stopping_.load();
        if (!keep_alive) {
            response.set_header("Connection", "close");
        }
        auto const out = response.serialize(request.method != fw::http::Method::head);
        co_await connection->write(out);
        if (!keep_alive) {
            break;
        }
    }

    connection->close();
    co_return;
}

} // namespace fw::server

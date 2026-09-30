#pragma once

// TCP primitives for the I/O layer. Like the rest of `framework/net`, this
// exposes Asio types; higher layers use `fw::net` names and senders.

#include "fw/net/io.hpp"

#include <asio.hpp>
#include <exec/asio/use_sender.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace fw::net {

class tcp_socket {
public:
    explicit tcp_socket(asio::ip::tcp::socket socket) : socket_(std::move(socket)) {}

    tcp_socket(tcp_socket&&) noexcept = default;
    tcp_socket& operator=(tcp_socket&&) noexcept = default;
    tcp_socket(tcp_socket const&) = delete;
    tcp_socket& operator=(tcp_socket const&) = delete;

    auto read_some(std::span<char> buffer) {
        return socket_.async_read_some(asio::buffer(buffer.data(), buffer.size()),
                                       experimental::execution::asio::use_sender);
    }

    auto write(std::span<char const> buffer) {
        return asio::async_write(socket_, asio::buffer(buffer.data(), buffer.size()),
                                 experimental::execution::asio::use_sender);
    }

    void close() {
        std::error_code ignored;
        socket_.close(ignored); // NOLINT(bugprone-unused-return-value)
    }

    [[nodiscard]] asio::ip::tcp::socket& native() noexcept {
        return socket_;
    }

private:
    asio::ip::tcp::socket socket_;
};

class tcp_listener {
public:
    tcp_listener(io_context& context, std::string const& host, std::uint16_t port)
        : acceptor_(context.get_executor()) {
        asio::ip::tcp::endpoint const endpoint{asio::ip::make_address(host), port};
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address{true});
        acceptor_.bind(endpoint);
        acceptor_.listen();
    }

    tcp_listener(tcp_listener const&) = delete;
    tcp_listener& operator=(tcp_listener const&) = delete;

    // The returned sender yields a connected `tcp_socket`. The listener must
    // outlive the operation.
    auto async_accept() {
        auto peer = std::make_shared<asio::ip::tcp::socket>(acceptor_.get_executor());
        return fw::just(peer) |
               fw::let_value([this](std::shared_ptr<asio::ip::tcp::socket> const& socket) {
                   return acceptor_.async_accept(*socket,
                                                 experimental::execution::asio::use_sender) |
                          fw::then([socket] { return tcp_socket{std::move(*socket)}; });
               });
    }

    void close() {
        std::error_code ignored;
        acceptor_.close(ignored); // NOLINT(bugprone-unused-return-value)
    }

    [[nodiscard]] std::uint16_t port() const {
        return acceptor_.local_endpoint().port();
    }
    [[nodiscard]] asio::ip::tcp::acceptor& native() noexcept {
        return acceptor_;
    }

private:
    asio::ip::tcp::acceptor acceptor_;
};

class tcp_connector {
public:
    tcp_connector(io_context& context, std::string host, std::uint16_t port)
        : executor_(context.get_executor()), host_(std::move(host)), port_(std::to_string(port)) {}

    tcp_connector(tcp_connector const&) = delete;
    tcp_connector& operator=(tcp_connector const&) = delete;

    // Resolves `host` and connects, yielding a `tcp_socket`. The connector must
    // outlive the operation.
    auto async_connect() {
        auto socket = std::make_shared<asio::ip::tcp::socket>(executor_);
        auto resolver = std::make_shared<asio::ip::tcp::resolver>(executor_);
        return fw::just(socket, resolver, host_, port_) |
               fw::let_value([](std::shared_ptr<asio::ip::tcp::socket> const& peer,
                                std::shared_ptr<asio::ip::tcp::resolver> const& res,
                                std::string const& host, std::string const& port) {
                   return res->async_resolve(host, port,
                                             experimental::execution::asio::use_sender) |
                          fw::let_value(
                              [peer, res](asio::ip::tcp::resolver::results_type const& results) {
                                  return asio::async_connect(
                                             *peer, results,
                                             experimental::execution::asio::use_sender) |
                                         fw::then([peer,
                                                   res](asio::ip::tcp::endpoint const& /*unused*/) {
                                             return tcp_socket{std::move(*peer)};
                                         });
                              });
               });
    }

private:
    asio::any_io_executor executor_;
    std::string host_;
    std::string port_;
};

} // namespace fw::net

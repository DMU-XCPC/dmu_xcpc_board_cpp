#include "fw/http/response.hpp"
#include "fw/net/io.hpp"
#include "fw/server/server.hpp"

#include <asio.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

namespace {

void configure_routes(fw::server::Router& router) {
    router.get("/health",
               fw::server::sync_handler([](fw::http::Request const&, fw::server::RouteContext&) {
                   return fw::http::Response::json(200, R"({"status":"ok"})");
               }));
    router.get("/api/contests/:id", fw::server::sync_handler([](fw::http::Request const&,
                                                                fw::server::RouteContext& context) {
                   auto const id = context.params().get("id").value_or("0");
                   return fw::http::Response::json(200, std::string{R"({"id":")"} +
                                                            std::string{id} + R"("})");
               }));
}

std::string send_and_receive(std::uint16_t port, std::string const& request) {
    asio::io_context client;
    asio::ip::tcp::socket socket{client};
    std::error_code ec;
    socket.connect({asio::ip::make_address("127.0.0.1"), port}, ec); // NOLINT
    if (ec) {
        return {};
    }
    (void)asio::write(socket, asio::buffer(request), ec);
    if (ec) {
        return {};
    }
    std::string response;
    (void)asio::read(socket, asio::dynamic_buffer(response), ec);
    return response;
}

} // namespace

TEST(Server, RespondsToGet) {
    fw::net::io_context io{2};
    fw::server::Server server{io, fw::server::ServerOptions{"127.0.0.1", 0}};
    configure_routes(server.router());
    server.start();
    ASSERT_NE(server.port(), 0);

    auto const response = send_and_receive(
        server.port(), "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find(R"({"status":"ok"})"), std::string::npos);

    server.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST(Server, CapturesRouteParam) {
    fw::net::io_context io{2};
    fw::server::Server server{io, fw::server::ServerOptions{"127.0.0.1", 0}};
    configure_routes(server.router());
    server.start();

    auto const response = send_and_receive(
        server.port(),
        "GET /api/contests/42 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find(R"({"id":"42"})"), std::string::npos);

    server.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST(Server, ReturnsNotFound) {
    fw::net::io_context io{2};
    fw::server::Server server{io, fw::server::ServerOptions{"127.0.0.1", 0}};
    configure_routes(server.router());
    server.start();

    auto const response = send_and_receive(
        server.port(), "GET /missing HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    EXPECT_NE(response.find("HTTP/1.1 404 Not Found"), std::string::npos);

    server.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST(Server, HandlesKeepAlive) {
    fw::net::io_context io{2};
    fw::server::Server server{io, fw::server::ServerOptions{"127.0.0.1", 0}};
    configure_routes(server.router());
    server.start();

    std::string const requests =
        "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    auto const response = send_and_receive(server.port(), requests);

    std::size_t count = 0;
    for (std::size_t pos = response.find("HTTP/1.1 200 OK"); pos != std::string::npos;
         pos = response.find("HTTP/1.1 200 OK", pos + 1)) {
        ++count;
    }
    EXPECT_EQ(count, 2U);

    server.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

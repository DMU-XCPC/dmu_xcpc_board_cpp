#include "fw/core/error.hpp"
#include "fw/http/client.hpp"
#include "fw/http/response.hpp"
#include "fw/net/io.hpp"
#include "fw/server/router.hpp"
#include "fw/server/server.hpp"

#include <asio.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <thread>
#include <tuple>
#include <utility>

namespace {

std::string gzip_bytes(std::initializer_list<unsigned char> data) {
    return std::string{reinterpret_cast<char const*>(data.begin()), data.size()};
}

void register_routes(fw::server::Router& router) {
    router.get("/health",
               fw::server::sync_handler([](fw::http::Request const&, fw::server::RouteContext&) {
                   return fw::http::Response::json(200, R"({"status":"ok"})");
               }));
    router.post("/echo", fw::server::sync_handler(
                             [](fw::http::Request const& request, fw::server::RouteContext&) {
                                 return fw::http::Response::text(200, request.body);
                             }));
    router.get("/old",
               fw::server::sync_handler([](fw::http::Request const&, fw::server::RouteContext&) {
                   return fw::http::Response::redirect(302, "/health");
               }));
    router.get("/gz",
               fw::server::sync_handler([](fw::http::Request const&, fw::server::RouteContext&) {
                   auto const compressed =
                       gzip_bytes({0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xcb,
                                   0x48, 0xcd, 0xc9, 0xc9, 0x57, 0x28, 0xcf, 0x2f, 0xca, 0x49, 0x01,
                                   0x00, 0x85, 0x11, 0x4a, 0x0d, 0x0b, 0x00, 0x00, 0x00});
                   auto response = fw::http::Response::text(200, compressed);
                   response.set_header("Content-Encoding", "gzip");
                   return response;
               }));
}

fw::http::ClientOptions client_options(std::uint16_t port) {
    fw::http::ClientOptions options;
    options.host = "127.0.0.1";
    options.port = port;
    return options;
}

class RawServer {
public:
    explicit RawServer(std::string response, std::chrono::milliseconds delay = {})
        : acceptor_(io_, asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), 0}),
          response_(std::move(response)), delay_(delay) {
        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address{true});
        acceptor_.listen();
        thread_ = std::thread([this] { serve(); });
    }

    ~RawServer() {
        stop();
    }

    RawServer(RawServer const&) = delete;
    RawServer& operator=(RawServer const&) = delete;

    [[nodiscard]] std::uint16_t port() const {
        return acceptor_.local_endpoint().port();
    }

    void stop() {
        std::error_code error;
        acceptor_.close(error); // NOLINT
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void serve() {
        std::error_code error;
        asio::ip::tcp::socket socket{io_};
        acceptor_.accept(socket, error); // NOLINT
        if (error) {
            return;
        }
        std::string buffer;
        asio::read_until(socket, asio::dynamic_buffer(buffer), "\r\n\r\n", error); // NOLINT
        if (error) {
            return;
        }
        if (delay_.count() > 0) {
            std::this_thread::sleep_for(delay_);
        }
        asio::write(socket, asio::buffer(response_), error); // NOLINT
        socket.close(error);                                 // NOLINT
    }

    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_;
    std::string response_;
    std::chrono::milliseconds delay_;
    std::thread thread_;
};

} // namespace

TEST(HttpClient, GetsHealthAndReusesConnection) {
    fw::net::io_context io{2};
    fw::server::Server server{io, fw::server::ServerOptions{"127.0.0.1", 0}};
    register_routes(server.router());
    server.start();

    fw::http::Client client{io, client_options(server.port())};

    auto first = std::get<0>(*fw::sync_wait(client.get("/health")));
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->status(), 200);
    EXPECT_EQ(first->body(), R"({"status":"ok"})");
    EXPECT_EQ(client.idle_connections(), 1U);

    auto second = std::get<0>(*fw::sync_wait(client.get("/health")));
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->status(), 200);
    EXPECT_EQ(client.idle_connections(), 1U);

    client.close();
    server.stop();
    server.wait();
}

TEST(HttpClient, PostsRequestBody) {
    fw::net::io_context io{2};
    fw::server::Server server{io, fw::server::ServerOptions{"127.0.0.1", 0}};
    register_routes(server.router());
    server.start();

    fw::http::Client client{io, client_options(server.port())};
    auto result = std::get<0>(*fw::sync_wait(client.post("/echo", "ping")));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status(), 200);
    EXPECT_EQ(result->body(), "ping");

    client.close();
    server.stop();
    server.wait();
}

TEST(HttpClient, FollowsRedirect) {
    fw::net::io_context io{2};
    fw::server::Server server{io, fw::server::ServerOptions{"127.0.0.1", 0}};
    register_routes(server.router());
    server.start();

    fw::http::Client client{io, client_options(server.port())};
    auto result = std::get<0>(*fw::sync_wait(client.get("/old")));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status(), 200);
    EXPECT_EQ(result->body(), R"({"status":"ok"})");

    client.close();
    server.stop();
    server.wait();
}

TEST(HttpClient, DecompressesGzipResponse) {
    fw::net::io_context io{2};
    fw::server::Server server{io, fw::server::ServerOptions{"127.0.0.1", 0}};
    register_routes(server.router());
    server.start();

    fw::http::Client client{io, client_options(server.port())};
    auto result = std::get<0>(*fw::sync_wait(client.get("/gz")));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->body(), "hello world");
    EXPECT_FALSE(result->header("Content-Encoding").has_value());

    client.close();
    server.stop();
    server.wait();
}

TEST(HttpClient, DecodesChunkedResponse) {
    RawServer const server{"HTTP/1.1 200 OK\r\n"
                           "Transfer-Encoding: chunked\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "5\r\nhello\r\n"
                           "6\r\n world\r\n"
                           "0\r\n\r\n"};
    fw::net::io_context io{1};
    fw::http::Client client{io, client_options(server.port())};

    auto result = std::get<0>(*fw::sync_wait(client.get("/")));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status(), 200);
    EXPECT_EQ(result->body(), "hello world");
}

TEST(HttpClient, ReadsBodyUntilClose) {
    RawServer const server{"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nraw body"};
    fw::net::io_context io{1};
    fw::http::Client client{io, client_options(server.port())};

    auto result = std::get<0>(*fw::sync_wait(client.get("/")));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->body(), "raw body");
}

TEST(HttpClient, TimesOutWhenNoResponse) {
    RawServer const server{"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok",
                           std::chrono::milliseconds{500}};
    fw::net::io_context io{1};
    auto options = client_options(server.port());
    options.timeout = std::chrono::milliseconds{100};
    fw::http::Client client{io, options};

    auto result = std::get<0>(*fw::sync_wait(client.get("/")));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), fw::make_error_code(fw::Errc::timed_out));
}

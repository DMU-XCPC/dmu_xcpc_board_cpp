#pragma once

#include "fw/core/error.hpp"
#include "fw/execution/task.hpp"
#include "fw/http/request.hpp"
#include "fw/http/response.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace fw::net {
class io_context;
}

namespace fw::http {

struct ClientOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{80};
    std::string user_agent{"fw-http-client/0.1"};
    std::chrono::milliseconds timeout{5000};
    std::chrono::milliseconds idle_timeout{15000};
    std::size_t max_response_bytes{8U << 20U};
    std::size_t max_redirects{5};
    std::size_t max_idle_connections{8};
    bool follow_redirects{true};
    bool decompress{true};
};

// An asynchronous HTTP/1.1 client. Connections are kept alive and reused from a
// small per-authority pool; requests time out, follow redirects and transparently
// decode gzip responses. Only plaintext HTTP is supported.
class Client {
public:
    Client(fw::net::io_context& context, ClientOptions options = {});
    ~Client();
    Client(Client const&) = delete;
    Client& operator=(Client const&) = delete;
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;

    [[nodiscard]] fw::task<fw::Result<Response>> send(Request request);
    [[nodiscard]] fw::task<fw::Result<Response>> get(std::string target);
    [[nodiscard]] fw::task<fw::Result<Response>>
    post(std::string target, std::string body, std::string content_type = "application/json");
    [[nodiscard]] fw::task<fw::Result<Response>> put(std::string target, std::string body,
                                                     std::string content_type = "application/json");
    [[nodiscard]] fw::task<fw::Result<Response>> del(std::string target);

    void close();
    [[nodiscard]] std::size_t idle_connections() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fw::http

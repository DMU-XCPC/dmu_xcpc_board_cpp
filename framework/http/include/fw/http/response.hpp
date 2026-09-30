#pragma once

#include "fw/http/request.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fw::http {

std::string_view reason_phrase(int status) noexcept;

class Response {
public:
    Response() = default;
    explicit Response(int status) : status_(status) {}

    void set_status(int status) noexcept {
        status_ = status;
    }
    void set_header(std::string name, std::string value);
    void remove_header(std::string_view name);
    void set_body(std::string body);
    void set_content_type(std::string value);

    [[nodiscard]] int status() const noexcept {
        return status_;
    }
    [[nodiscard]] std::string const& body() const noexcept {
        return body_;
    }
    [[nodiscard]] std::optional<std::string_view> header(std::string_view name) const;

    // Serializes the response. When `include_body` is false only the status line
    // and headers are emitted (used for HEAD responses).
    [[nodiscard]] std::string serialize(bool include_body = true) const;

    static Response text(int status, std::string body);
    static Response json(int status, std::string body);
    static Response redirect(int status, std::string location);
    static Response no_content();

private:
    int status_ = 200;
    std::vector<Header> headers_;
    std::string body_;
};

} // namespace fw::http

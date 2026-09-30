#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fw::http {

[[nodiscard]] inline bool iequals(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        auto const a = static_cast<unsigned char>(lhs[i]);
        auto const b = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

enum class Method : std::uint8_t {
    get,
    head,
    post,
    put,
    delete_,
    patch,
    options,
    connect,
    trace,
    other,
};

std::string_view to_string(Method method) noexcept;
Method method_from_string(std::string_view name) noexcept;

struct Header {
    std::string name;
    std::string value;
};

class Request {
public:
    Method method = Method::get;
    std::string target;
    std::string path;
    std::string query;
    int minor_version = 1;
    std::vector<Header> headers;
    std::string body;

    [[nodiscard]] std::optional<std::string_view> header(std::string_view name) const;
    [[nodiscard]] bool has_header(std::string_view name) const;

    [[nodiscard]] std::string serialize() const;

    void clear();
};

} // namespace fw::http

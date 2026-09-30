#include "fw/http/parser.hpp"

#include "fw/http/request.hpp"

#include <picohttpparser.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace fw::http {
namespace {

constexpr std::size_t max_headers = 64;

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

bool parse_content_length(std::string_view value, std::size_t& out) {
    value = trim(value);
    if (value.empty()) {
        return false;
    }
    std::uint64_t parsed = 0;
    auto const* const begin = value.data();
    auto const* const end = value.data() + value.size();
    auto const [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) {
        return false;
    }
    out = static_cast<std::size_t>(parsed);
    return true;
}

} // namespace

ParseResult parse_request(std::string_view buffer, Request& request) {
    char const* method = nullptr;
    std::size_t method_len = 0;
    char const* target = nullptr;
    std::size_t target_len = 0;
    int minor_version = 0;
    std::array<phr_header, max_headers> headers{};
    std::size_t num_headers = headers.size();

    int const header_bytes =
        phr_parse_request(buffer.data(), buffer.size(), &method, &method_len, &target, &target_len,
                          &minor_version, headers.data(), &num_headers, 0);
    if (header_bytes == -2) {
        return {ParseState::incomplete, 0};
    }
    if (header_bytes < 0) {
        return {ParseState::error, 0};
    }

    std::size_t content_length = 0;
    for (std::size_t i = 0; i < num_headers; ++i) {
        std::string_view const name{headers[i].name, headers[i].name_len};
        std::string_view const value{headers[i].value, headers[i].value_len};
        if (iequals(name, "Content-Length")) {
            if (!parse_content_length(value, content_length)) {
                return {ParseState::error, 0};
            }
        } else if (iequals(name, "Transfer-Encoding") && !iequals(trim(value), "identity")) {
            return {ParseState::error, 0};
        }
    }

    auto const total = static_cast<std::size_t>(header_bytes) + content_length;
    if (buffer.size() < total) {
        return {ParseState::incomplete, 0};
    }

    request.clear();
    request.method = method_from_string(std::string_view{method, method_len});
    std::string_view const raw_target{target, target_len};
    request.target.assign(raw_target);
    auto const query_pos = raw_target.find('?');
    if (query_pos == std::string_view::npos) {
        request.path.assign(raw_target);
    } else {
        request.path.assign(raw_target.substr(0, query_pos));
        request.query.assign(raw_target.substr(query_pos + 1));
    }
    request.minor_version = minor_version;
    request.headers.reserve(num_headers);
    for (std::size_t i = 0; i < num_headers; ++i) {
        request.headers.push_back(Header{
            std::string{headers[i].name, headers[i].name_len},
            std::string{headers[i].value, headers[i].value_len},
        });
    }
    if (content_length > 0) {
        request.body.assign(buffer.substr(static_cast<std::size_t>(header_bytes), content_length));
    }

    return {ParseState::complete, total};
}

} // namespace fw::http

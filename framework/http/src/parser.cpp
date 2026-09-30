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

bool has_token(std::string_view value, std::string_view token) {
    std::size_t pos = 0;
    while (pos <= value.size()) {
        auto const comma = value.find(',', pos);
        auto const part = value.substr(pos, comma == std::string_view::npos ? std::string_view::npos
                                                                            : comma - pos);
        if (iequals(trim(part), token)) {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        pos = comma + 1;
    }
    return false;
}

bool parse_hex(std::string_view value, std::size_t& out) {
    value = trim(value);
    auto const semi = value.find(';');
    if (semi != std::string_view::npos) {
        value = trim(value.substr(0, semi));
    }
    if (value.empty()) {
        return false;
    }
    std::uint64_t parsed = 0;
    auto const* const begin = value.data();
    auto const* const end = value.data() + value.size();
    auto const [ptr, ec] = std::from_chars(begin, end, parsed, 16);
    if (ec != std::errc{} || ptr != end) {
        return false;
    }
    out = static_cast<std::size_t>(parsed);
    return true;
}

struct ChunkScan {
    bool complete = false;
    bool error = false;
    std::size_t end = 0;
    std::string body;
};

ChunkScan scan_chunked(std::string_view buffer, std::size_t start) {
    ChunkScan result;
    std::size_t pos = start;
    while (true) {
        auto const line_end = buffer.find("\r\n", pos);
        if (line_end == std::string_view::npos) {
            return result;
        }
        std::size_t size = 0;
        if (!parse_hex(buffer.substr(pos, line_end - pos), size)) {
            result.error = true;
            return result;
        }
        pos = line_end + 2;
        if (size == 0) {
            while (true) {
                auto const trailer_end = buffer.find("\r\n", pos);
                if (trailer_end == std::string_view::npos) {
                    return result;
                }
                if (trailer_end == pos) {
                    result.complete = true;
                    result.end = pos + 2;
                    return result;
                }
                pos = trailer_end + 2;
            }
        }
        if (buffer.size() < pos + size + 2) {
            return result;
        }
        result.body.append(buffer.substr(pos, size));
        pos += size;
        if (buffer.substr(pos, 2) != "\r\n") {
            result.error = true;
            return result;
        }
        pos += 2;
    }
}

struct BodyInfo {
    bool chunked = false;
    bool has_length = false;
    bool ok = true;
    std::size_t length = 0;
};

BodyInfo inspect_body(phr_header const* headers, std::size_t count) {
    BodyInfo info;
    for (std::size_t i = 0; i < count; ++i) {
        std::string_view const name{headers[i].name, headers[i].name_len};
        std::string_view const value{headers[i].value, headers[i].value_len};
        if (iequals(name, "Content-Length")) {
            if (!parse_content_length(value, info.length)) {
                info.ok = false;
                return info;
            }
            info.has_length = true;
        } else if (iequals(name, "Transfer-Encoding")) {
            if (has_token(value, "chunked")) {
                info.chunked = true;
            } else if (!iequals(trim(value), "identity")) {
                info.ok = false;
                return info;
            }
        }
    }
    return info;
}

BodyFraming pick_framing(BodyInfo const& info, int status, bool head_request) {
    if (head_request || (status >= 100 && status < 200) || status == 204 || status == 304) {
        return BodyFraming::none;
    }
    if (info.chunked) {
        return BodyFraming::chunked;
    }
    if (info.has_length) {
        return BodyFraming::content_length;
    }
    return BodyFraming::until_close;
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

ResponseParseResult parse_response(std::string_view buffer, Response& response, bool head_request) {
    int minor_version = 0;
    int status = 0;
    char const* message = nullptr;
    std::size_t message_len = 0;
    std::array<phr_header, max_headers> headers{};
    std::size_t num_headers = headers.size();

    int const header_bytes =
        phr_parse_response(buffer.data(), buffer.size(), &minor_version, &status, &message,
                           &message_len, headers.data(), &num_headers, 0);
    if (header_bytes == -2) {
        return {ParseState::incomplete, 0, BodyFraming::none, 0};
    }
    if (header_bytes < 0) {
        return {ParseState::error, 0, BodyFraming::none, 0};
    }
    auto const header_size = static_cast<std::size_t>(header_bytes);

    auto const body = inspect_body(headers.data(), num_headers);
    if (!body.ok) {
        return {ParseState::error, 0, BodyFraming::none, 0};
    }

    response = Response{status};
    for (std::size_t i = 0; i < num_headers; ++i) {
        response.set_header(std::string{headers[i].name, headers[i].name_len},
                            std::string{headers[i].value, headers[i].value_len});
    }

    switch (pick_framing(body, status, head_request)) {
    case BodyFraming::none:
        return {ParseState::complete, header_size, BodyFraming::none, 0};
    case BodyFraming::content_length: {
        auto const total = header_size + body.length;
        if (buffer.size() < total) {
            return {ParseState::incomplete, 0, BodyFraming::content_length, body.length};
        }
        response.set_body(std::string{buffer.substr(header_size, body.length)});
        return {ParseState::complete, total, BodyFraming::content_length, body.length};
    }
    case BodyFraming::chunked: {
        auto scan = scan_chunked(buffer, header_size);
        if (scan.error) {
            return {ParseState::error, 0, BodyFraming::chunked, 0};
        }
        if (!scan.complete) {
            return {ParseState::incomplete, 0, BodyFraming::chunked, 0};
        }
        response.set_body(std::move(scan.body));
        return {ParseState::complete, scan.end, BodyFraming::chunked, 0};
    }
    case BodyFraming::until_close:
        return {ParseState::incomplete, header_size, BodyFraming::until_close, 0};
    }
    return {ParseState::error, 0, BodyFraming::none, 0};
}

} // namespace fw::http

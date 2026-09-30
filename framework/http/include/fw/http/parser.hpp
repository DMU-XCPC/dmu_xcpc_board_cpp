#pragma once

#include "fw/http/request.hpp"
#include "fw/http/response.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fw::http {

enum class ParseState : std::uint8_t { complete, incomplete, error };

struct ParseResult {
    ParseState state = ParseState::incomplete;
    std::size_t consumed = 0;
};

// Parses a complete HTTP/1.1 request (headers plus a Content-Length body) from
// `buffer`. `consumed` is the number of bytes belonging to the request once the
// state is `complete`. On `incomplete` the caller should read more and retry;
// `request` is left untouched.
ParseResult parse_request(std::string_view buffer, Request& request);

enum class BodyFraming : std::uint8_t { none, content_length, chunked, until_close };

struct ResponseParseResult {
    ParseState state = ParseState::incomplete;
    std::size_t consumed = 0;
    BodyFraming framing = BodyFraming::none;
    std::size_t body_length = 0;
};

// Parses an HTTP/1.1 response from `buffer`. `head_request` must be true when
// the original request used HEAD (the response then carries no body). While
// headers are still incomplete the state is `incomplete` and `framing` is
// `none`; once headers are complete `framing` tells the caller how to obtain
// the body. For `until_close` the state stays `incomplete` and `consumed` is the
// header length, so the caller can collect the body until EOF. On `complete`,
// `consumed` is the full message length and `response` is populated.
ResponseParseResult parse_response(std::string_view buffer, Response& response,
                                   bool head_request = false);

} // namespace fw::http

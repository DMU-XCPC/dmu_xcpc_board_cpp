#pragma once

#include "fw/http/request.hpp"

#include <cstddef>
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

} // namespace fw::http

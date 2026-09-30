#pragma once

#include "fw/core/error.hpp"

#include <string>
#include <string_view>

namespace fw::http {

// Decompresses a gzip stream (RFC 1952). Concatenated members are decoded into
// a single buffer. Returns an error on corrupt input.
fw::Result<std::string> gzip_decompress(std::string_view input);

} // namespace fw::http

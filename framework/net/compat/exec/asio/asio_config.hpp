// Standalone-Asio configuration for stdexec's <exec/asio/...> integration.
// Mirrors the generated `asio_config.hpp` from stdexec's CMake (see
// third_party/stdexec/include/exec/asio/asio_config.hpp.in). Keeping it here
// avoids patching the vendored submodule.
#pragma once

#define STDEXEC_ASIO_USES_STANDALONE 1
#define STDEXEC_ASIO_USES_BOOST 0

#include <system_error>

#include <asio.hpp>

#define ASIOEXEC_ASIO_NAMESPACE asio

namespace experimental::execution::asio {
namespace asio_impl = ::asio;
using std::errc;
using std::error_code;
using std::error_condition;
using std::system_error;
} // namespace experimental::execution::asio

namespace exec = experimental::execution;

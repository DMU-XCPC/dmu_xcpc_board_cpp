#pragma once

#include <stdexec/stop_token.hpp>

namespace fw {

// Cancellation primitives re-exported from the execution layer so the rest of
// the framework can request and observe cancellation without naming stdexec.
using stdexec::inplace_stop_source;
using stdexec::inplace_stop_token;
using stdexec::never_stop_token;

} // namespace fw

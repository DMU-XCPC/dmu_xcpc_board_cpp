#pragma once

#include "fw/execution/task.hpp"

#include <exec/start_detached.hpp>
#include <stdexec/execution.hpp>

namespace fw {

// A manually driven, thread-safe task queue for one thread (typically the main
// thread). Other threads post work to `get_scheduler()`; the owning thread
// calls `run()` and blocks until `finish()` is called.
using stdexec::run_loop;

// Fire-and-forget a sender; the sender must not be able to complete with an
// error (wrap it in `upon_error` first if it can).
using exec::start_detached;

} // namespace fw

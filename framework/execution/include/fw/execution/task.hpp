#pragma once

// The execution layer is the single place in the framework allowed to expose
// stdexec types. Everything else depends on these `fw::` aliases instead.

#include <exec/task.hpp>
#include <stdexec/execution.hpp>

namespace fw {

namespace execution {

template <class T = void> using task = stdexec::task<T>;
using stdexec::continues_on;
using stdexec::just;
using stdexec::let_error;
using stdexec::let_value;
using stdexec::on;
using stdexec::starts_on;
using stdexec::sync_wait;
using stdexec::then;
using stdexec::upon_error;
using stdexec::when_all;

} // namespace execution

template <class T = void> using task = execution::task<T>;

using execution::continues_on;
using execution::just;
using execution::let_error;
using execution::let_value;
using execution::on;
using execution::starts_on;
using execution::sync_wait;
using execution::then;
using execution::upon_error;
using execution::when_all;
} // namespace fw

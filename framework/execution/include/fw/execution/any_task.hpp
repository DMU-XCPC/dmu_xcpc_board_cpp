#pragma once

#include "fw/execution/task.hpp"

#include <utility>

namespace fw::execution {

// The unified result of an asynchronous operation in this framework. It is the
// single, concrete task type, so any handler (sync or async) can present the
// same result type without additional type erasure.
template <class T> using any_task = fw::task<T>;

// Wrap an already-available value as a ready task.
template <class T> fw::task<T> ready(T value) {
    co_return value;
}

} // namespace fw::execution

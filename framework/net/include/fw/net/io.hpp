#pragma once

// I/O layer. Like `framework/execution`, this module sits on the execution
// surface, so it is allowed to expose Asio and stdexec's Asio integration
// types. Higher layers should only use the `fw::net` names.

#include "fw/execution/task.hpp"

#include <asio.hpp>
#include <exec/asio/asio_thread_pool.hpp>
#include <exec/asio/use_sender.hpp>
#include <exec/start_detached.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

namespace fw::net {

class io_context {
public:
    using scheduler = experimental::execution::asio::asio_thread_pool::scheduler;

    explicit io_context(std::uint32_t threads = 1) : pool_(threads) {}

    io_context(io_context const&) = delete;
    io_context& operator=(io_context const&) = delete;
    io_context(io_context&&) = delete;
    io_context& operator=(io_context&&) = delete;

    [[nodiscard]] scheduler get_scheduler() noexcept {
        return pool_.get_scheduler();
    }
    [[nodiscard]] std::uint32_t available_parallelism() const {
        return pool_.available_parallelism();
    }
    [[nodiscard]] asio::any_io_executor get_executor() const {
        return pool_.get_executor();
    }

private:
    experimental::execution::asio::asio_thread_pool pool_;
};

// Delivers POSIX signals as an asynchronous sender, so a program can block on
// a signal without spinning or using async-signal-unsafe handlers.
class signal_waiter {
public:
    signal_waiter(io_context& context, std::vector<int> const& signals)
        : signals_(context.get_executor()) {
        for (int const signal : signals) {
            signals_.add(signal);
        }
    }

    // Completes with the delivered signal number.
    auto async_wait() {
        return signals_.async_wait(experimental::execution::asio::use_sender);
    }

private:
    asio::signal_set signals_;
};

// Run `handler` on an I/O worker the next time any of `signals` is delivered.
// Signals stay an application concern: the framework only provides this hook.
template <class Handler>
void on_signal(io_context& context, std::vector<int> const& signals, Handler handler) {
    auto waiter = std::make_shared<signal_waiter>(context, signals);
    auto task = [](std::shared_ptr<signal_waiter> waiter_in, Handler handler_in) -> fw::task<void> {
        (void)co_await waiter_in->async_wait();
        handler_in();
        co_return;
    }(std::move(waiter), std::move(handler));
    exec::start_detached(fw::starts_on(context.get_scheduler(), std::move(task)) |
                         fw::upon_error([](std::exception_ptr const&) noexcept {}));
}

inline auto async_sleep(io_context& context, std::chrono::steady_clock::duration duration) {
    auto timer = std::make_shared<asio::steady_timer>(context.get_executor());
    timer->expires_after(duration);
    return fw::just(timer) | fw::let_value([](std::shared_ptr<asio::steady_timer> const& t) {
               return t->async_wait(experimental::execution::asio::use_sender);
           });
}

} // namespace fw::net

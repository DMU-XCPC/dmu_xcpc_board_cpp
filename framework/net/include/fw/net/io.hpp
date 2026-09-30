#pragma once

// I/O layer. Like `framework/execution`, this module sits on the execution
// surface, so it is allowed to expose Asio and stdexec's Asio integration
// types. Higher layers should only use the `fw::net` names.

#include "fw/execution/task.hpp"

#include <asio.hpp>
#include <exec/asio/asio_thread_pool.hpp>
#include <exec/asio/use_sender.hpp>

#include <chrono>
#include <cstdint>
#include <memory>

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

inline auto async_sleep(io_context& context, std::chrono::steady_clock::duration duration) {
    auto timer = std::make_shared<asio::steady_timer>(context.get_executor());
    timer->expires_after(duration);
    return fw::just(timer) | fw::let_value([](std::shared_ptr<asio::steady_timer> const& t) {
               return t->async_wait(experimental::execution::asio::use_sender);
           });
}

} // namespace fw::net

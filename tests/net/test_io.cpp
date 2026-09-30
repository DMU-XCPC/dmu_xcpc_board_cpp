#include "fw/net/io.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <tuple>

TEST(Net, SleepCompletes) {
    fw::net::io_context context{1};

    auto result = fw::sync_wait(fw::net::async_sleep(context, std::chrono::milliseconds(5)));
    EXPECT_TRUE(result.has_value());
}

TEST(Net, SchedulerRunsWorkOffTheCallingThread) {
    fw::net::io_context context{1};
    auto const caller = std::this_thread::get_id();

    auto work = fw::starts_on(context.get_scheduler(),
                              fw::just() | fw::then([] { return std::this_thread::get_id(); }));
    auto result = fw::sync_wait(work);

    ASSERT_TRUE(result.has_value());
    EXPECT_NE(std::get<0>(*result), caller);
}

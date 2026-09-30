#include "fw/execution/run_loop.hpp"

#include <gtest/gtest.h>

#include <thread>

TEST(RunLoop, PostsFromAnotherThread) {
    fw::run_loop loop;

    std::thread producer{[&loop] {
        fw::start_detached(fw::starts_on(loop.get_scheduler(), fw::just()) |
                           fw::then([&loop] noexcept { loop.finish(); }));
    }};

    loop.run();
    producer.join();
    SUCCEED();
}

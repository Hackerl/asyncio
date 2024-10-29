#include "catch_extensions.h"
#include <asyncio/signal.h>
#include <catch2/catch_test_macros.hpp>
#include <unistd.h>
#include <thread>

ASYNC_TEST_CASE("signal", "[signal]") {
    auto signal = asyncio::Signal::make();
    REQUIRE(signal);

    SECTION("normal") {
        using namespace std::chrono_literals;

        std::thread thread{[] {
            std::this_thread::sleep_for(20ms);
            kill(getpid(), SIGINT);
        }};

        REQUIRE(co_await signal->on(SIGINT));
        thread.join();
    }

    SECTION("cancel") {
        auto task = signal->on(SIGINT);
        REQUIRE(task.cancel());
        REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
    }
}

#include "catch_extensions.h"
#include <asyncio/signal.h>
#include <zero/defer.h>
#include <unistd.h>
#include <thread>

ASYNC_TEST_CASE("signal", "[signal]") {
    auto signal = asyncio::Signal::make();
    REQUIRE(signal);

    SECTION("normal") {
        using namespace std::chrono_literals;

        std::thread thread{
            [] {
                std::this_thread::sleep_for(20ms);
                kill(getpid(), SIGINT);
            }
        };

        DEFER(thread.join());
        REQUIRE(co_await signal->on(SIGINT));
    }

    SECTION("cancel") {
        auto task = signal->on(SIGINT);
        REQUIRE(task.cancel());
        REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
    }
}

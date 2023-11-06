#include <asyncio/ev/timer.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("timer", "[timer]") {
    SECTION("normal") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto timer = asyncio::ev::makeTimer();
            REQUIRE(timer);

            auto tp = std::chrono::system_clock::now();
            co_await timer->after(100ms);
            REQUIRE(std::chrono::system_clock::now() - tp > 95ms);
        });
    }

    SECTION("cancel") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto timer = asyncio::ev::makeTimer();
            REQUIRE(timer);

            auto task = timer->after(100ms);
            auto result = co_await asyncio::timeout(task, 50ms);

            REQUIRE(task.done());
            REQUIRE(task.result().error() == std::errc::operation_canceled);
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        });
    }
}
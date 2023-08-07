#include <asyncio/ev/timer.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("timer", "[timer]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        auto timer = asyncio::ev::makeTimer();
        REQUIRE(timer);

        auto tp = std::chrono::system_clock::now();
        co_await timer->after(100ms);
        REQUIRE(std::chrono::system_clock::now() - tp > 100ms);
    });
}
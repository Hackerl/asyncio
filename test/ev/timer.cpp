#include <asyncio/ev/timer.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("timer", "[ev]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("normal") {
            auto timer = asyncio::ev::Timer::make();
            REQUIRE(timer);

            const auto tp = std::chrono::system_clock::now();
            co_await timer->after(50ms);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("timeout") {
            auto timer = asyncio::ev::Timer::make();
            REQUIRE(timer);

            const auto result = co_await asyncio::timeout(timer->after(50ms), 20ms);
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        }
    });
}

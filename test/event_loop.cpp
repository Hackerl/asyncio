#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asyncio event loop", "[event loop]") {
    SECTION("sleep") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto tp = std::chrono::system_clock::now();
            co_await asyncio::sleep(50ms);
            REQUIRE(std::chrono::system_clock::now() - tp > 50ms);
        });
    }

    SECTION("timeout") {
        SECTION("success") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto result = co_await asyncio::timeout(asyncio::sleep(10ms), 50ms);
                REQUIRE(result);
            });
        }

        SECTION("failure") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto result = co_await asyncio::timeout(asyncio::sleep(50ms), 10ms);
                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::timed_out);
            });
        }
    }
}
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asyncio event loop", "[event loop]") {
    SECTION("sleep") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            const auto tp = std::chrono::system_clock::now();
            co_await asyncio::sleep(50ms);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        });
    }

    SECTION("timeout") {
        SECTION("success") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                const auto result = co_await asyncio::timeout(asyncio::sleep(10ms), 50ms);
                REQUIRE(result);
            });
        }

        SECTION("timeout") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                const auto result = co_await asyncio::timeout(asyncio::sleep(50ms), 10ms);
                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::timed_out);
            });
        }

        SECTION("failure") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto task = asyncio::sleep(50ms);
                task.cancel();
                const auto result = co_await asyncio::timeout(task, 50ms);
                REQUIRE(result);
                REQUIRE(result.value().error() == std::errc::operation_canceled);
            });
        }

        SECTION("cancel") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto task = asyncio::timeout(asyncio::sleep(50ms), 50ms);
                task.cancel();
                const auto result = co_await task;
                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::operation_canceled);
            });
        }
    }
}

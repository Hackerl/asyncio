#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asyncio event loop", "[event loop]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("sleep") {
            const auto tp = std::chrono::system_clock::now();
            co_await asyncio::sleep(50ms);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("timeout") {
            SECTION("success") {
                const auto result = co_await asyncio::timeout(asyncio::sleep(10ms), 20ms);
                REQUIRE(result);
            }

            SECTION("timeout") {
                const auto result = co_await asyncio::timeout(asyncio::sleep(20ms), 10ms);
                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::timed_out);
            }

            SECTION("failure") {
                auto task = asyncio::sleep(50ms);
                task.cancel();
                const auto result = co_await asyncio::timeout(task, 20ms);
                REQUIRE(result);
                REQUIRE(result.value().error() == std::errc::operation_canceled);
            }

            SECTION("cancel") {
                auto task = asyncio::timeout(asyncio::sleep(20ms), 20ms);
                task.cancel();
                const auto result = co_await task;
                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::operation_canceled);
            }
        }
    });
}

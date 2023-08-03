#include <asyncio/thread.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asynchronously run in a separate thread", "[thread]") {
    SECTION("no result") {
        SECTION("no error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                co_await asyncio::toThread([]() {
                    auto tp = std::chrono::system_clock::now();
                    std::this_thread::sleep_for(50ms);
                    REQUIRE(std::chrono::system_clock::now() - tp > 50ms);
                });
            });
        }

        SECTION("error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto result = co_await asyncio::toThread([]() -> tl::expected<void, int> {
                    std::this_thread::sleep_for(100ms);
                    return tl::unexpected(-1);
                });

                REQUIRE(!result);
                REQUIRE(result.error() == -1);
            });
        }
    }

    SECTION("have result") {
        SECTION("no error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto result = co_await asyncio::toThread([]() {
                    std::this_thread::sleep_for(100ms);
                    return 1024;
                });

                REQUIRE(result);
                REQUIRE(result.value() == 1024);
            });
        }

        SECTION("error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto result = co_await asyncio::toThread([]() -> tl::expected<int, int> {
                    std::this_thread::sleep_for(100ms);
                    return tl::unexpected(-1);
                });

                REQUIRE(!result);
                REQUIRE(result.error() == -1);
                result.value();
            });
        }
    }
}
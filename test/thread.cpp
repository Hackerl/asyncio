#include <asyncio/thread.h>
#include <catch2/catch_test_macros.hpp>
#include <thread>

TEST_CASE("asynchronously run in a separate thread", "[thread]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        SECTION("void") {
            using namespace std::chrono_literals;

            const auto tp = std::chrono::system_clock::now();
            const auto res = co_await asyncio::toThread([] {
                std::this_thread::sleep_for(50ms);
            });
            REQUIRE(res);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("not void") {
            using namespace std::chrono_literals;

            const auto tp = std::chrono::system_clock::now();
            const auto res = co_await asyncio::toThread([] {
                std::this_thread::sleep_for(50ms);
                return 1024;
            });
            REQUIRE(res);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
            REQUIRE(*res == 1024);
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

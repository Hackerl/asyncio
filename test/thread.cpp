#include <asyncio/thread.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asynchronously run in a separate thread", "[thread]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("no result") {
            SECTION("no error") {
                const auto tp = std::chrono::system_clock::now();

                co_await asyncio::toThread([]() -> tl::expected<void, std::error_code> {
                    std::this_thread::sleep_for(50ms);
                    return {};
                });

                REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
            }

            SECTION("error") {
                const auto result = co_await asyncio::toThread([]() -> tl::expected<void, std::error_code> {
                    std::this_thread::sleep_for(10ms);
                    return tl::unexpected(make_error_code(std::errc::bad_message));
                });

                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::bad_message);
            }
        }

        SECTION("have result") {
            SECTION("no error") {
                const auto result = co_await asyncio::toThread([]() -> tl::expected<int, std::error_code> {
                    std::this_thread::sleep_for(10ms);
                    return 1024;
                });

                REQUIRE(result);
                REQUIRE(*result == 1024);
            }

            SECTION("error") {
                const auto result = co_await asyncio::toThread([]() -> tl::expected<int, std::error_code> {
                    std::this_thread::sleep_for(10ms);
                    return tl::unexpected(make_error_code(std::errc::bad_message));
                });

                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::bad_message);
            }
        }

        SECTION("timeout") {
            const auto stop = std::make_shared<bool>();

            auto task = asyncio::toThread(
                [=]() -> tl::expected<int, std::error_code> {
                    while (!*stop)
                        std::this_thread::sleep_for(10ms);

                    return tl::unexpected(make_error_code(std::errc::operation_canceled));
                },
                [=](std::thread::native_handle_type) -> tl::expected<void, std::error_code> {
                    *stop = true;
                    return {};
                }
            );

            co_await asyncio::sleep(10ms);
            REQUIRE(!task.done());

            task.cancel();
            const auto result = co_await task;
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::operation_canceled);
        }
    });
}

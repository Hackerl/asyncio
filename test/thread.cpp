#include <asyncio/thread.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asynchronously run in a separate thread", "[thread]") {
    SECTION("no result") {
        SECTION("no error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                co_await asyncio::toThread([]() -> tl::expected<void, std::error_code> {
                    auto tp = std::chrono::system_clock::now();
                    std::this_thread::sleep_for(50ms);
                    REQUIRE(std::chrono::system_clock::now() - tp > 50ms);
                    return {};
                });
            });
        }

        SECTION("error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto result = co_await asyncio::toThread([]() -> tl::expected<void, std::error_code> {
                    std::this_thread::sleep_for(10ms);
                    return tl::unexpected(make_error_code(std::errc::bad_message));
                });

                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::bad_message);
            });
        }
    }

    SECTION("have result") {
        SECTION("no error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto result = co_await asyncio::toThread([]() -> tl::expected<int, std::error_code> {
                    std::this_thread::sleep_for(10ms);
                    return 1024;
                });

                REQUIRE(result);
                REQUIRE(*result == 1024);
            });
        }

        SECTION("error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto result = co_await asyncio::toThread([]() -> tl::expected<int, std::error_code> {
                    std::this_thread::sleep_for(10ms);
                    return tl::unexpected(make_error_code(std::errc::bad_message));
                });

                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::bad_message);
            });
        }
    }

    SECTION("cancel") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            std::shared_ptr<bool> stop = std::make_shared<bool>();

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
            co_await task;

            REQUIRE(task.done());
            REQUIRE(task.result().error() == std::errc::operation_canceled);
        });
    }
}
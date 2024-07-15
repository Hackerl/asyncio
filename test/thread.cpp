#include <asyncio/thread.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("asynchronously run in a separate thread", "[thread]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        const auto tp = std::chrono::system_clock::now();

        SECTION("thread") {
            SECTION("void") {
                using namespace std::chrono_literals;

                co_await asyncio::toThread([] {
                    std::this_thread::sleep_for(50ms);
                });
                REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
            }

            SECTION("not void") {
                using namespace std::chrono_literals;

                const auto res = co_await asyncio::toThread([] {
                    std::this_thread::sleep_for(50ms);
                    return 1024;
                });
                REQUIRE(res == 1024);
                REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
            }

            SECTION("cancellable") {
                SECTION("void") {
                    using namespace std::chrono_literals;

                    zero::atomic::Event event;

                    auto task = asyncio::toThread(
                        [&]() -> std::expected<void, std::error_code> {
                            if (event.wait(50ms))
                                return std::unexpected(asyncio::task::Error::CANCELLED);

                            return {};
                        },
                        [&](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
                            event.set();
                            return {};
                        }
                    );

                    SECTION("normal") {
                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());
                        const auto res = co_await task;
                        REQUIRE(!res);
                        REQUIRE(res.error() == std::errc::operation_canceled);
                        REQUIRE(std::chrono::system_clock::now() - tp < 50ms);
                    }
                }

                SECTION("not void") {
                    using namespace std::chrono_literals;

                    zero::atomic::Event event;

                    auto task = asyncio::toThread(
                        [&]() -> std::expected<int, std::error_code> {
                            if (event.wait(50ms))
                                return std::unexpected(asyncio::task::Error::CANCELLED);

                            return 1024;
                        },
                        [&](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
                            event.set();
                            return {};
                        }
                    );

                    SECTION("normal") {
                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(*res == 1024);
                        REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());
                        const auto res = co_await task;
                        REQUIRE(!res);
                        REQUIRE(res.error() == std::errc::operation_canceled);
                        REQUIRE(std::chrono::system_clock::now() - tp < 50ms);
                    }
                }
            }
        }

        SECTION("thread poll") {
            SECTION("void") {
                using namespace std::chrono_literals;

                const auto res = co_await asyncio::toThreadPool([] {
                    std::this_thread::sleep_for(50ms);
                });
                REQUIRE(res);
                REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
            }

            SECTION("not void") {
                using namespace std::chrono_literals;

                const auto res = co_await asyncio::toThreadPool([] {
                    std::this_thread::sleep_for(50ms);
                    return 1024;
                });
                REQUIRE(res);
                REQUIRE(*res == 1024);
                REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
            }
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

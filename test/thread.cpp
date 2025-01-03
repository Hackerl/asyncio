#include "catch_extensions.h"
#include <asyncio/thread.h>
#include <catch2/catch_test_macros.hpp>

ASYNC_TEST_CASE("post task to a new thread", "[thread]") {
    using namespace std::chrono_literals;

    const auto tp = std::chrono::system_clock::now();

    SECTION("void") {
        co_await asyncio::toThread([] {
            std::this_thread::sleep_for(50ms);
        });
        REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
    }

    SECTION("not void") {
        const auto result = co_await asyncio::toThread([] {
            std::this_thread::sleep_for(50ms);
            return 1024;
        });
        REQUIRE(result == 1024);
        REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
    }
}

ASYNC_TEST_CASE("post cancellable task to a new thread", "[thread]") {
    const auto tp = std::chrono::system_clock::now();

    SECTION("void") {
        using namespace std::chrono_literals;

        zero::atomic::Event event;

        auto task = asyncio::toThread(
            [&]() -> std::expected<void, std::error_code> {
                if (event.wait(50ms))
                    return std::unexpected{asyncio::task::Error::CANCELLED};

                return {};
            },
            [&](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
                event.set();
                return {};
            }
        );

        SECTION("normal") {
            REQUIRE(co_await task);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
            REQUIRE(std::chrono::system_clock::now() - tp < 50ms);
        }
    }

    SECTION("not void") {
        using namespace std::chrono_literals;

        zero::atomic::Event event;

        auto task = asyncio::toThread(
            [&]() -> std::expected<int, std::error_code> {
                if (event.wait(50ms))
                    return std::unexpected{asyncio::task::Error::CANCELLED};

                return 1024;
            },
            [&](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
                event.set();
                return {};
            }
        );

        SECTION("normal") {
            REQUIRE(co_await task == 1024);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
            REQUIRE(std::chrono::system_clock::now() - tp < 50ms);
        }
    }
}

ASYNC_TEST_CASE("post task to thread pool", "[thread]") {
    using namespace std::chrono_literals;

    const auto tp = std::chrono::system_clock::now();

    SECTION("void") {
        const auto result = co_await asyncio::toThreadPool([] {
            std::this_thread::sleep_for(50ms);
        });
        REQUIRE(result);
        REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
    }

    SECTION("not void") {
        const auto result = co_await asyncio::toThreadPool([] {
            std::this_thread::sleep_for(50ms);
            return 1024;
        });
        REQUIRE(result == 1024);
        REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
    }
}

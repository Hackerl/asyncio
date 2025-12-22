#include "catch_extensions.h"
#include <asyncio/time.h>
#include <catch2/matchers/catch_matchers_all.hpp>

ASYNC_TEST_CASE("sleep", "[time]") {
    using namespace std::chrono_literals;
    const auto tp = std::chrono::system_clock::now();
    REQUIRE(co_await asyncio::sleep(50ms));
    REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
}

ASYNC_TEST_CASE("timeout - error", "[time]") {
    using namespace std::chrono_literals;

    SECTION("not expired") {
        REQUIRE(co_await asyncio::timeout(asyncio::sleep(10ms), 20ms));
    }

    SECTION("expired") {
        REQUIRE_ERROR(co_await asyncio::timeout(asyncio::sleep(20ms), 10ms), asyncio::TimeoutError::ELAPSED);
    }

    SECTION("expired but cannot be cancelled") {
        asyncio::Promise<void, std::error_code> promise;

        auto task = asyncio::timeout(
            from(asyncio::task::CancellableFuture{
                promise.getFuture(),
                []() -> std::expected<void, std::error_code> {
                    return std::unexpected{asyncio::task::Error::CANCELLATION_TOO_LATE};
                }
            }),
            10ms
        );

        REQUIRE(co_await asyncio::sleep(20ms));
        REQUIRE_FALSE(task.done());

        promise.resolve();

        const auto result = co_await task;
        REQUIRE(result);
        REQUIRE(*result);
    }

    SECTION("cancel") {
        auto task = asyncio::timeout(asyncio::sleep(20ms), 10ms);
        REQUIRE(task.cancel());

        const auto result = co_await task;
        REQUIRE(result);
        REQUIRE_FALSE(*result);
        REQUIRE(result->error() == std::errc::operation_canceled);
    }
}

ASYNC_TEST_CASE("timeout - exception", "[time]") {
    using namespace std::chrono_literals;

    SECTION("not expired") {
        REQUIRE_NOTHROW(
            co_await asyncio::timeout(
                asyncio::task::spawn([]() -> asyncio::task::Task<void> {
                    zero::error::guard(co_await asyncio::sleep(10ms));
                }),
                20ms
            )
        );
    }

    SECTION("expired") {
        REQUIRE_THROWS_MATCHES(
            co_await asyncio::timeout(
                asyncio::task::spawn([]() -> asyncio::task::Task<void> {
                    zero::error::guard(co_await asyncio::sleep(20ms));
                }),
                10ms
            ),
            std::system_error,
            Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                return error.code() == asyncio::TimeoutError::ELAPSED;
            })
        );
    }

    SECTION("expired but cannot be cancelled") {
        asyncio::Promise<void, std::error_code> promise;

        auto task = asyncio::timeout(
            asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
                zero::error::guard(co_await from(asyncio::task::CancellableFuture{
                    promise.getFuture(),
                    []() -> std::expected<void, std::error_code> {
                        return std::unexpected{asyncio::task::Error::CANCELLATION_TOO_LATE};
                    }
                }));
            }),
            10ms
        );

        REQUIRE(co_await asyncio::sleep(20ms));
        REQUIRE_FALSE(task.done());

        promise.resolve();
        REQUIRE_NOTHROW(co_await task);
    }

    SECTION("cancel") {
        auto task = asyncio::timeout(
            asyncio::task::spawn([]() -> asyncio::task::Task<void> {
                zero::error::guard(co_await asyncio::sleep(20ms));
            }),
            10ms
        );
        REQUIRE(task.cancel());

        REQUIRE_THROWS_MATCHES(
            co_await task,
            std::system_error,
            Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                return error.code() == std::errc::operation_canceled;
            })
        );
    }
}

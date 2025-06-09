#include "catch_extensions.h"
#include <asyncio/time.h>

ASYNC_TEST_CASE("sleep", "[time]") {
    using namespace std::chrono_literals;
    const auto tp = std::chrono::system_clock::now();
    REQUIRE(co_await asyncio::sleep(50ms));
    REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
}

ASYNC_TEST_CASE("timeout", "[time]") {
    using namespace std::chrono_literals;

    SECTION("not expired") {
        REQUIRE(co_await asyncio::timeout(asyncio::sleep(10ms), 20ms));
    }

    SECTION("expired") {
        REQUIRE_ERROR(co_await asyncio::timeout(asyncio::sleep(20ms), 10ms), asyncio::TimeoutError::ELAPSED);
    }

    SECTION("expired but cannot be cancelled") {
        const auto promise = std::make_shared<asyncio::Promise<void, std::error_code>>();
        auto task = asyncio::timeout(
            from(asyncio::task::CancellableFuture{
                promise->getFuture(),
                [=]() -> std::expected<void, std::error_code> {
                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};
                }
            }),
            10ms
        );

        promise->resolve();

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

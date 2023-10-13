#include <asyncio/sync/mutex.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asyncio mutex", "[mutex]") {
    SECTION("normal") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto mutex = std::make_shared<asyncio::sync::Mutex>();
            auto result = co_await mutex->lock();
            REQUIRE(result);

            co_await zero::async::coroutine::allSettled(
                    [](auto mutex) -> zero::async::coroutine::Task<void> {
                        auto result = co_await mutex->lock();
                        REQUIRE(result);
                        mutex->unlock();
                    }(mutex),
                    [](auto mutex) -> zero::async::coroutine::Task<void> {
                        auto result = co_await mutex->lock();
                        REQUIRE(result);
                        mutex->unlock();
                    }(mutex),
                    [](auto mutex) -> zero::async::coroutine::Task<void> {
                        auto result = co_await mutex->lock();
                        REQUIRE(result);
                        mutex->unlock();
                    }(mutex),
                    [](auto mutex) -> zero::async::coroutine::Task<void> {
                        co_await asyncio::sleep(50ms);
                        mutex->unlock();
                    }(mutex)
            );
        });
    }

    SECTION("cancel") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto mutex = std::make_shared<asyncio::sync::Mutex>();
            auto result = co_await mutex->lock();
            REQUIRE(result);

            auto task = zero::async::coroutine::allSettled(
                    [](auto mutex) -> zero::async::coroutine::Task<void> {
                        auto result = co_await mutex->lock();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::operation_canceled);
                    }(mutex),
                    [](auto mutex) -> zero::async::coroutine::Task<void> {
                        auto result = co_await mutex->lock();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::operation_canceled);
                    }(mutex),
                    [](auto mutex) -> zero::async::coroutine::Task<void> {
                        auto result = co_await mutex->lock();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::operation_canceled);
                    }(mutex)
            );

            task.cancel();
            co_await task;
        });
    }
}
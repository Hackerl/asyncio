#include <asyncio/sync/mutex.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asyncio mutex", "[mutex]") {
    SECTION("normal") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            const auto mutex = std::make_shared<asyncio::sync::Mutex>();
            const auto result = co_await mutex->lock();
            REQUIRE(result);

            co_await allSettled(
                [](auto m) -> zero::async::coroutine::Task<void> {
                    auto res = co_await m->lock();
                    REQUIRE(res);
                    m->unlock();
                }(mutex),
                [](auto m) -> zero::async::coroutine::Task<void> {
                    auto res = co_await m->lock();
                    REQUIRE(res);
                    m->unlock();
                }(mutex),
                [](auto m) -> zero::async::coroutine::Task<void> {
                    auto res = co_await m->lock();
                    REQUIRE(res);
                    m->unlock();
                }(mutex),
                [](auto m) -> zero::async::coroutine::Task<void> {
                    co_await asyncio::sleep(50ms);
                    m->unlock();
                }(mutex)
            );
        });
    }

    SECTION("cancel") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            const auto mutex = std::make_shared<asyncio::sync::Mutex>();
            const auto result = co_await mutex->lock();
            REQUIRE(result);

            auto task = allSettled(
                [](auto m) -> zero::async::coroutine::Task<void> {
                    auto res = co_await m->lock();
                    REQUIRE(!res);
                    REQUIRE(res.error() == std::errc::operation_canceled);
                }(mutex),
                [](auto m) -> zero::async::coroutine::Task<void> {
                    auto res = co_await m->lock();
                    REQUIRE(!res);
                    REQUIRE(res.error() == std::errc::operation_canceled);
                }(mutex),
                [](auto m) -> zero::async::coroutine::Task<void> {
                    auto res = co_await m->lock();
                    REQUIRE(!res);
                    REQUIRE(res.error() == std::errc::operation_canceled);
                }(mutex)
            );

            task.cancel();
            co_await task;
        });
    }
}

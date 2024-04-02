#include <asyncio/sync/mutex.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asyncio mutex", "[sync]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        const auto mutex = std::make_shared<asyncio::sync::Mutex>();
        REQUIRE(!mutex->locked());

        const auto result = co_await mutex->lock();
        REQUIRE(result);
        REQUIRE(mutex->locked());

        SECTION("normal") {
            co_await allSettled(
                [](auto m) -> zero::async::coroutine::Task<void> {
                    const auto res = co_await m->lock();
                    REQUIRE(res);
                    m->unlock();
                }(mutex),
                [](auto m) -> zero::async::coroutine::Task<void> {
                    const auto res = co_await m->lock();
                    REQUIRE(res);
                    m->unlock();
                }(mutex),
                [](auto m) -> zero::async::coroutine::Task<void> {
                    const auto res = co_await m->lock();
                    REQUIRE(res);
                    m->unlock();
                }(mutex),
                [](auto m) -> zero::async::coroutine::Task<void> {
                    co_await asyncio::sleep(20ms);
                    m->unlock();
                }(mutex)
            );
            REQUIRE(!mutex->locked());
        }

        SECTION("fair scheduling") {
            auto task1 = mutex->lock();
            REQUIRE(!task1.done());

            mutex->unlock();
            REQUIRE(!task1.done());

            auto task2 = mutex->lock();
            REQUIRE(!task2.done());

            auto res = co_await task1;
            REQUIRE(res);

            mutex->unlock();
            res = co_await task2;
            REQUIRE(res);
        }

        SECTION("timeout") {
            co_await allSettled(
                [](auto m) -> zero::async::coroutine::Task<void> {
                    const auto res = co_await m->lock(10ms);
                    REQUIRE(!res);
                    REQUIRE(res.error() == std::errc::timed_out);
                }(mutex),
                [](auto m) -> zero::async::coroutine::Task<void> {
                    const auto res = co_await m->lock();
                    REQUIRE(res);
                    m->unlock();
                }(mutex),
                [](auto m) -> zero::async::coroutine::Task<void> {
                    const auto res = co_await m->lock();
                    REQUIRE(res);
                    m->unlock();
                }(mutex),
                [](auto m) -> zero::async::coroutine::Task<void> {
                    co_await asyncio::sleep(20ms);
                    m->unlock();
                }(mutex)
            );
            REQUIRE(!mutex->locked());
        }

        SECTION("cancel") {
            auto task = allSettled(
                [](auto m) -> zero::async::coroutine::Task<void> {
                    const auto res = co_await m->lock();
                    REQUIRE(!res);
                    REQUIRE(res.error() == std::errc::operation_canceled);
                }(mutex),
                [](auto m) -> zero::async::coroutine::Task<void> {
                    const auto res = co_await m->lock();
                    REQUIRE(!res);
                    REQUIRE(res.error() == std::errc::operation_canceled);
                }(mutex),
                [](auto m) -> zero::async::coroutine::Task<void> {
                    const auto res = co_await m->lock();
                    REQUIRE(!res);
                    REQUIRE(res.error() == std::errc::operation_canceled);
                }(mutex)
            );

            task.cancel();
            co_await task;
            REQUIRE(mutex->locked());
        }

        SECTION("cancel after unlock") {
            auto task1 = mutex->lock();
            REQUIRE(!task1.done());

            mutex->unlock();

            REQUIRE(!task1.cancel());
            auto task2 = mutex->lock();
            REQUIRE(!task2.done());

            auto res = co_await task1;
            REQUIRE(res);
            REQUIRE(mutex->locked());

            mutex->unlock();

            res = co_await task2;
            REQUIRE(res);
            REQUIRE(mutex->locked());
        }

        SECTION("unlock after cancel") {
            auto task1 = mutex->lock();
            REQUIRE(!task1.done());
            REQUIRE(task1.cancel());
            REQUIRE(!task1.done());

            mutex->unlock();

            auto task2 = mutex->lock();
            REQUIRE(!task2.done());

            auto res = co_await task1;
            REQUIRE(!res);
            REQUIRE(res.error() == std::errc::operation_canceled);

            res = co_await task2;
            REQUIRE(res);
            REQUIRE(mutex->locked());
        }
    });
}

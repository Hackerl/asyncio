#include <asyncio/sync/mutex.h>
#include <asyncio/time.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("asyncio mutex", "[sync]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        const auto mutex = std::make_shared<asyncio::sync::Mutex>();
        REQUIRE(!mutex->locked());

        const auto res = co_await mutex->lock();
        REQUIRE(res);
        REQUIRE(mutex->locked());

        SECTION("normal") {
            co_await allSettled(
                [](auto m) -> asyncio::task::Task<void> {
                    const auto r = co_await m->lock();
                    REQUIRE(r);
                    m->unlock();
                }(mutex),
                [](auto m) -> asyncio::task::Task<void> {
                    const auto r = co_await m->lock();
                    REQUIRE(r);
                    m->unlock();
                }(mutex),
                [](auto m) -> asyncio::task::Task<void> {
                    const auto r = co_await m->lock();
                    REQUIRE(r);
                    m->unlock();
                }(mutex),
                [](auto m) -> asyncio::task::Task<void> {
                    using namespace std::chrono_literals;
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

            auto r = co_await task1;
            REQUIRE(r);

            mutex->unlock();
            r = co_await task2;
            REQUIRE(r);
        }

        SECTION("timeout") {
            co_await allSettled(
                [](auto m) -> asyncio::task::Task<void> {
                    using namespace std::chrono_literals;
                    const auto r = co_await asyncio::timeout(m->lock(), 10ms);
                    REQUIRE(!r);
                    REQUIRE(r.error() == asyncio::TimeoutError::ELAPSED);
                }(mutex),
                [](auto m) -> asyncio::task::Task<void> {
                    const auto r = co_await m->lock();
                    REQUIRE(r);
                    m->unlock();
                }(mutex),
                [](auto m) -> asyncio::task::Task<void> {
                    const auto r = co_await m->lock();
                    REQUIRE(r);
                    m->unlock();
                }(mutex),
                [](auto m) -> asyncio::task::Task<void> {
                    using namespace std::chrono_literals;
                    co_await asyncio::sleep(20ms);
                    m->unlock();
                }(mutex)
            );
            REQUIRE(!mutex->locked());
        }

        SECTION("cancel") {
            auto task = allSettled(
                [](auto m) -> asyncio::task::Task<void> {
                    const auto r = co_await m->lock();
                    REQUIRE(!r);
                    REQUIRE(r.error() == std::errc::operation_canceled);
                }(mutex),
                [](auto m) -> asyncio::task::Task<void> {
                    const auto r = co_await m->lock();
                    REQUIRE(!r);
                    REQUIRE(r.error() == std::errc::operation_canceled);
                }(mutex),
                [](auto m) -> asyncio::task::Task<void> {
                    const auto r = co_await m->lock();
                    REQUIRE(!r);
                    REQUIRE(r.error() == std::errc::operation_canceled);
                }(mutex)
            );

            REQUIRE(task.cancel());
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

            auto r = co_await task1;
            REQUIRE(r);
            REQUIRE(mutex->locked());

            mutex->unlock();

            r = co_await task2;
            REQUIRE(r);
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

            auto r = co_await task1;
            REQUIRE(!r);
            REQUIRE(r.error() == std::errc::operation_canceled);

            r = co_await task2;
            REQUIRE(r);
            REQUIRE(mutex->locked());
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

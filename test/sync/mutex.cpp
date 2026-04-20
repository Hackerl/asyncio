#include <catch_extensions.h>
#include <asyncio/sync/mutex.h>
#include <asyncio/error.h>

ASYNC_TEST_CASE("mutex", "[sync::mutex]") {
    asyncio::sync::Mutex mutex;
    REQUIRE_FALSE(mutex.locked());

    co_await asyncio::error::guard(mutex.lock());
    REQUIRE(mutex.locked());

    SECTION("normal") {
        auto task = mutex.lock();

        co_await asyncio::error::guard(asyncio::reschedule());
        REQUIRE_FALSE(task.done());

        mutex.unlock();
        REQUIRE(co_await task);
    }

    SECTION("fair scheduling") {
        auto task1 = mutex.lock();
        REQUIRE_FALSE(task1.done());

        mutex.unlock();
        REQUIRE_FALSE(task1.done());

        auto task2 = mutex.lock();
        REQUIRE_FALSE(task2.done());

        REQUIRE(co_await task1);
        REQUIRE(mutex.locked());

        mutex.unlock();

        REQUIRE(co_await task2);
        REQUIRE(mutex.locked());
        mutex.unlock();
    }

    SECTION("cancel") {
        auto task = mutex.lock();
        REQUIRE(task.cancel());
        REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
    }

    SECTION("cancel after unlock") {
        auto task1 = mutex.lock();
        REQUIRE_FALSE(task1.done());

        mutex.unlock();
        REQUIRE_ERROR(task1.cancel(), asyncio::task::Error::CancellationTooLate);

        auto task2 = mutex.lock();
        REQUIRE_FALSE(task2.done());

        REQUIRE(co_await task1);
        REQUIRE(mutex.locked());

        mutex.unlock();
        REQUIRE(co_await task2);
        REQUIRE(mutex.locked());
    }

    SECTION("unlock after cancel") {
        auto task1 = mutex.lock();
        REQUIRE_FALSE(task1.done());
        REQUIRE(task1.cancel());

        mutex.unlock();

        auto task2 = mutex.lock();
        REQUIRE_FALSE(task2.done());
        REQUIRE_ERROR(co_await task1, std::errc::operation_canceled);

        REQUIRE(co_await task2);
        REQUIRE(mutex.locked());
    }
}

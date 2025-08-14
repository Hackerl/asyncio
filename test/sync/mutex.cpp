#include <catch_extensions.h>
#include <asyncio/time.h>
#include <asyncio/sync/mutex.h>

ASYNC_TEST_CASE("mutex", "[sync::mutex]") {
    asyncio::sync::Mutex mutex;
    REQUIRE_FALSE(mutex.locked());

    REQUIRE(co_await mutex.lock());
    REQUIRE(mutex.locked());

    SECTION("normal") {
        using namespace std::chrono_literals;

        auto task = mutex.lock();
        REQUIRE_FALSE(task.done());

        REQUIRE(co_await asyncio::sleep(20ms));
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
        REQUIRE_ERROR(task1.cancel(), asyncio::task::Error::WILL_BE_DONE);

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
        REQUIRE_FALSE(task1.done());

        mutex.unlock();

        auto task2 = mutex.lock();
        REQUIRE_FALSE(task2.done());
        REQUIRE_ERROR(co_await task1, std::errc::operation_canceled);

        REQUIRE(co_await task2);
        REQUIRE(mutex.locked());
    }
}

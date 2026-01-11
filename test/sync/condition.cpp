#include "catch_extensions.h"
#include <asyncio/sync/condition.h>
#include <asyncio/time.h>

ASYNC_TEST_CASE("condition variable", "[sync::condition]") {
    asyncio::sync::Condition condition;
    asyncio::sync::Mutex mutex;

    SECTION("notify") {
        using namespace std::chrono_literals;

        REQUIRE(co_await mutex.lock());

        auto task = condition.wait(mutex);
        REQUIRE_FALSE(mutex.locked());

        REQUIRE(co_await asyncio::sleep(20ms));
        REQUIRE_FALSE(task.done());

        condition.notify();
        REQUIRE(co_await task);
        REQUIRE(mutex.locked());
    }

    SECTION("broadcast") {
        using namespace std::chrono_literals;

        REQUIRE(co_await mutex.lock());

        auto task1 = condition.wait(mutex);
        REQUIRE_FALSE(mutex.locked());

        REQUIRE(co_await mutex.lock());

        auto task2 = condition.wait(mutex);
        REQUIRE_FALSE(mutex.locked());

        REQUIRE(co_await asyncio::sleep(20ms));
        REQUIRE_FALSE(task1.done());
        REQUIRE_FALSE(task2.done());

        condition.broadcast();
        REQUIRE(co_await task1);
        REQUIRE(mutex.locked());

        mutex.unlock();
        REQUIRE(co_await task2);
        REQUIRE(mutex.locked());
    }

    SECTION("predicate") {
        using namespace std::chrono_literals;

        REQUIRE(co_await mutex.lock());

        int value{};

        auto task = condition.wait(
            mutex,
            [&] {
                return value == 1;
            }
        );
        REQUIRE_FALSE(mutex.locked());

        REQUIRE(co_await asyncio::sleep(20ms));
        REQUIRE_FALSE(task.done());

        condition.notify();

        REQUIRE(co_await asyncio::sleep(20ms));
        REQUIRE_FALSE(task.done());

        value = 1;
        condition.notify();
        REQUIRE(co_await task);
        REQUIRE(mutex.locked());
    }

    SECTION("cancel") {
        REQUIRE(co_await mutex.lock());

        auto task = condition.wait(mutex);
        REQUIRE_FALSE(mutex.locked());
        REQUIRE(task.cancel());

        REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
        REQUIRE(mutex.locked());
    }

    SECTION("cancel after notify") {
        REQUIRE(co_await mutex.lock());

        auto task = condition.wait(mutex);
        REQUIRE_FALSE(mutex.locked());

        condition.notify();
        REQUIRE_ERROR(task.cancel(), asyncio::task::Error::CANCELLATION_TOO_LATE);

        REQUIRE(co_await task);
        REQUIRE(mutex.locked());
    }

    SECTION("notify after cancel") {
        REQUIRE(co_await mutex.lock());

        auto task1 = condition.wait(mutex);
        REQUIRE_FALSE(task1.done());
        REQUIRE_FALSE(mutex.locked());
        REQUIRE(task1.cancel());

        condition.notify();

        REQUIRE(co_await mutex.lock());

        auto task2 = condition.wait(mutex);
        REQUIRE_FALSE(task2.done());
        REQUIRE_FALSE(mutex.locked());

        REQUIRE_ERROR(co_await task1, std::errc::operation_canceled);
        REQUIRE(mutex.locked());

        mutex.unlock();
        REQUIRE(co_await task2);
        REQUIRE(mutex.locked());
    }
}

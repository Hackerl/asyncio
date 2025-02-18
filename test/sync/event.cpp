#include <catch_extensions.h>
#include <asyncio/sync/event.h>
#include <asyncio/time.h>

ASYNC_TEST_CASE("event", "[sync]") {
    asyncio::sync::Event event;
    REQUIRE_FALSE(event.isSet());

    SECTION("normal") {
        using namespace std::chrono_literals;

        auto task1 = event.wait();
        REQUIRE_FALSE(task1.done());

        auto task2 = event.wait();
        REQUIRE_FALSE(task2.done());

        co_await asyncio::sleep(20ms);

        REQUIRE_FALSE(event.isSet());
        REQUIRE_FALSE(task1.done());
        REQUIRE_FALSE(task2.done());

        event.set();
        REQUIRE(event.isSet());

        REQUIRE(co_await task1);
        REQUIRE(co_await task2);
    }

    SECTION("cancel") {
        auto task = event.wait();
        REQUIRE(task.cancel());
        REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
    }
}

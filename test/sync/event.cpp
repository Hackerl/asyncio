#include <catch_extensions.h>
#include <asyncio/sync/event.h>
#include <asyncio/error.h>

ASYNC_TEST_CASE("event", "[sync::event]") {
    asyncio::sync::Event event;
    REQUIRE_FALSE(event.isSet());

    SECTION("normal") {
        auto task1 = event.wait();
        auto task2 = event.wait();

        co_await asyncio::error::guard(asyncio::reschedule());
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

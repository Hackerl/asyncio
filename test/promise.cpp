#include "catch_extensions.h"
#include <asyncio/sync/event.h>
#include <catch2/catch_test_macros.hpp>

ASYNC_TEST_CASE("promise", "[promise]") {
    asyncio::Promise<void> promise;
    asyncio::sync::Event event;

    promise.getFuture().then([&] {
        event.set();
    });

    promise.resolve();
    REQUIRE_FALSE(event.isSet());
    co_await event.wait();
}

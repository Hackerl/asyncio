#include "catch_extensions.h"
#include <asyncio/sync/event.h>

ASYNC_TEST_CASE("promise", "[promise]") {
    asyncio::sync::Event event;

    auto [promise, future] = contract<void>(asyncio::getEventLoop());

    std::move(future).then([&] {
        event.set();
    });

    promise.resolve();
    REQUIRE_FALSE(event.isSet());
    REQUIRE(co_await event.wait());
}

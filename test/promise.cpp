#include <asyncio/promise.h>
#include <asyncio/sync/event.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("asyncio promise", "[promise]") {
    const auto result = asyncio::run([]() -> zero::async::coroutine::Task<void> {
        asyncio::Promise<void> promise;
        const auto event = std::make_shared<asyncio::sync::Event>();
        REQUIRE(!event->isSet());

        promise.getFuture().then([=] {
            event->set();
        });

        promise.resolve();
        REQUIRE(!event->isSet());
        co_await event->wait();
    });
    REQUIRE(result);
    REQUIRE(*result);
}

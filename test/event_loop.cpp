#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asyncio event loop", "[event loop]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        co_await asyncio::sleep(50ms);
    });
}
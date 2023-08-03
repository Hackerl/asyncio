#include <asyncio/ev/signal.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <csignal>

using namespace std::chrono_literals;

TEST_CASE("signal handler", "[signal]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        asyncio::ev::Signal signal(SIGINT);
        auto task = signal.on();

        co_await zero::async::coroutine::all(
                []() -> zero::async::coroutine::Task<void> {
                    co_await asyncio::sleep(100ms);
                    raise(SIGINT);
                }(),
                [&]() -> zero::async::coroutine::Task<void> {
                    auto result = co_await task;
                    REQUIRE(result);
                }()
        );
    });
}
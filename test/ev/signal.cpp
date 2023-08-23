#include <asyncio/ev/signal.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <csignal>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace std::chrono_literals;

TEST_CASE("signal handler", "[signal]") {
    SECTION("normal") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto signal = asyncio::ev::makeSignal(SIGINT);
            REQUIRE(signal);

            auto task = signal->on();

            co_await zero::async::coroutine::allSettled(
                    []() -> zero::async::coroutine::Task<void> {
                        co_await asyncio::sleep(20ms);
#ifdef _WIN32
                        raise(SIGINT);
#else
                        kill(getpid(), SIGINT);
#endif
                    }(),
                    [&]() -> zero::async::coroutine::Task<void> {
                        auto result = co_await task;
                        REQUIRE(result);
                    }()
            );
        });
    }

    SECTION("cancel") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto signal = asyncio::ev::makeSignal(SIGINT);
            REQUIRE(signal);

            auto task = signal->on();
            auto result = co_await asyncio::timeout(task, 50ms);

            REQUIRE(task.done());
            REQUIRE(task.result().error() == std::errc::operation_canceled);
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        });
    }
}
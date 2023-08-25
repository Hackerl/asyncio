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

            std::thread thread([]() {
                std::this_thread::sleep_for(50ms);
#ifdef _WIN32
                raise(SIGINT);
#else
                kill(getpid(), SIGINT);
#endif
            });

            auto result = co_await signal->on();
            REQUIRE(result);

            thread.join();
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
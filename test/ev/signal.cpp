#include <asyncio/ev/signal.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <csignal>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace std::chrono_literals;

TEST_CASE("signal handler", "[ev]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("normal") {
            auto signal = asyncio::ev::Signal::make(SIGINT);
            REQUIRE(signal);

#ifdef __APPLE__
            std::thread thread([]() {
                std::this_thread::sleep_for(20ms);
                kill(getpid(), SIGINT);
            });

            const auto result = co_await signal->on();
            REQUIRE(result);

            thread.join();
#else
            auto task = signal->on();
            raise(SIGINT);

            const auto result = co_await task;
            REQUIRE(result);
#endif
        }

        SECTION("timeout") {
            auto signal = asyncio::ev::Signal::make(SIGINT);
            REQUIRE(signal);

            const auto result = co_await asyncio::timeout(signal->on(), 10ms);
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        }
    });
}

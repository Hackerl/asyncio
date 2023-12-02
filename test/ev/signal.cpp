#include <asyncio/ev/signal.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <csignal>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace std::chrono_literals;

TEST_CASE("signal handler", "[signal]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("normal") {
            auto signal = asyncio::ev::makeSignal(SIGINT);
            REQUIRE(signal);

#ifdef __APPLE__
            std::thread thread([]() {
                std::this_thread::sleep_for(50ms);
                kill(getpid(), SIGINT);
            });

            const auto result = co_await signal->on();
            REQUIRE(result);

            thread.join();
#else
            const auto task = signal->on();
            raise(SIGINT);

            const auto result = co_await task;
            REQUIRE(result);
#endif
        }

        SECTION("cancel") {
            auto signal = asyncio::ev::makeSignal(SIGINT);
            REQUIRE(signal);

            const auto task = signal->on();
            const auto result = co_await asyncio::timeout(task, 10ms);

            REQUIRE(task.done());
            REQUIRE(task.result().error() == std::errc::operation_canceled);
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        }
    });
}

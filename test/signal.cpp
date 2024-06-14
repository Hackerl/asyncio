#include <asyncio/signal.h>
#include <asyncio/time.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <unistd.h>
#include <thread>

TEST_CASE("signal handler", "[ev]") {
    const auto result = asyncio::run([]() -> zero::async::coroutine::Task<void> {
        auto signal = asyncio::Signal::make();
        REQUIRE(signal);

        SECTION("normal") {
            using namespace std::chrono_literals;

            std::thread thread([]() {
                std::this_thread::sleep_for(20ms);
                kill(getpid(), SIGINT);
            });

            const auto res = co_await signal->on(SIGINT);
            REQUIRE(res);

            thread.join();
        }

        SECTION("timeout") {
            using namespace std::chrono_literals;
            const auto res = co_await asyncio::timeout(signal->on(SIGINT), 10ms);
            REQUIRE(!res);
            REQUIRE(res.error() == asyncio::TimeoutError::ELAPSED);
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

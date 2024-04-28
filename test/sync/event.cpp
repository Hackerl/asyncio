#include <asyncio/sync/event.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asyncio event", "[sync]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        const auto event = std::make_shared<asyncio::sync::Event>();
        REQUIRE(!event->isSet());

        SECTION("normal") {
            co_await allSettled(
                [](auto e) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await e->wait();
                    REQUIRE(result);
                    REQUIRE(e->isSet());
                }(event),
                [](auto e) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await e->wait();
                    REQUIRE(result);
                    REQUIRE(e->isSet());
                }(event),
                [](auto e) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await e->wait();
                    REQUIRE(result);
                    REQUIRE(e->isSet());
                }(event),
                [](auto e) -> zero::async::coroutine::Task<void> {
                    co_await asyncio::sleep(20ms);
                    REQUIRE(!e->isSet());
                    e->set();
                    REQUIRE(e->isSet());
                }(event)
            );
        }

        SECTION("timeout") {
            co_await allSettled(
                [](auto e) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await asyncio::timeout(e->wait(), 10ms);
                    REQUIRE(!result);
                    REQUIRE(result.error() == asyncio::TimeoutError::ELAPSED);
                    REQUIRE(!e->isSet());
                }(event),
                [](auto e) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await e->wait();
                    REQUIRE(result);
                    REQUIRE(e->isSet());
                }(event),
                [](auto e) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await e->wait();
                    REQUIRE(result);
                    REQUIRE(e->isSet());
                }(event),
                [](auto e) -> zero::async::coroutine::Task<void> {
                    co_await asyncio::sleep(20ms);
                    REQUIRE(!e->isSet());
                    e->set();
                    REQUIRE(e->isSet());
                }(event)
            );
        }

        SECTION("cancel") {
            auto task = allSettled(
                [](auto e) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await e->wait();
                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::operation_canceled);
                    REQUIRE(!e->isSet());
                }(event),
                [](auto e) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await e->wait();
                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::operation_canceled);
                    REQUIRE(!e->isSet());
                }(event),
                [](auto e) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await e->wait();
                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::operation_canceled);
                    REQUIRE(!e->isSet());
                }(event)
            );

            REQUIRE(task.cancel());
            co_await task;
        }
    });
}

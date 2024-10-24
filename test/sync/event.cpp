#include <asyncio/sync/event.h>
#include <asyncio/time.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("asyncio event", "[sync]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        const auto event = std::make_shared<asyncio::sync::Event>();
        REQUIRE_FALSE(event->isSet());

        SECTION("normal") {
            co_await allSettled(
                [](auto e) -> asyncio::task::Task<void> {
                    const auto res = co_await e->wait();
                    REQUIRE(res);
                    REQUIRE(e->isSet());
                }(event),
                [](auto e) -> asyncio::task::Task<void> {
                    const auto res = co_await e->wait();
                    REQUIRE(res);
                    REQUIRE(e->isSet());
                }(event),
                [](auto e) -> asyncio::task::Task<void> {
                    const auto res = co_await e->wait();
                    REQUIRE(res);
                    REQUIRE(e->isSet());
                }(event),
                [](auto e) -> asyncio::task::Task<void> {
                    using namespace std::chrono_literals;
                    co_await asyncio::sleep(20ms);
                    REQUIRE_FALSE(e->isSet());
                    e->set();
                    REQUIRE(e->isSet());
                }(event)
            );
        }

        SECTION("timeout") {
            co_await allSettled(
                [](auto e) -> asyncio::task::Task<void> {
                    using namespace std::chrono_literals;
                    const auto res = co_await asyncio::timeout(e->wait(), 10ms);
                    REQUIRE_FALSE(res);
                    REQUIRE(res.error() == asyncio::TimeoutError::ELAPSED);
                    REQUIRE_FALSE(e->isSet());
                }(event),
                [](auto e) -> asyncio::task::Task<void> {
                    const auto res = co_await e->wait();
                    REQUIRE(res);
                    REQUIRE(e->isSet());
                }(event),
                [](auto e) -> asyncio::task::Task<void> {
                    const auto res = co_await e->wait();
                    REQUIRE(res);
                    REQUIRE(e->isSet());
                }(event),
                [](auto e) -> asyncio::task::Task<void> {
                    using namespace std::chrono_literals;
                    co_await asyncio::sleep(20ms);
                    REQUIRE_FALSE(e->isSet());
                    e->set();
                    REQUIRE(e->isSet());
                }(event)
            );
        }

        SECTION("cancel") {
            auto task = allSettled(
                [](auto e) -> asyncio::task::Task<void> {
                    const auto res = co_await e->wait();
                    REQUIRE_FALSE(res);
                    REQUIRE(res.error() == std::errc::operation_canceled);
                    REQUIRE_FALSE(e->isSet());
                }(event),
                [](auto e) -> asyncio::task::Task<void> {
                    const auto res = co_await e->wait();
                    REQUIRE_FALSE(res);
                    REQUIRE(res.error() == std::errc::operation_canceled);
                    REQUIRE_FALSE(e->isSet());
                }(event),
                [](auto e) -> asyncio::task::Task<void> {
                    const auto res = co_await e->wait();
                    REQUIRE_FALSE(res);
                    REQUIRE(res.error() == std::errc::operation_canceled);
                    REQUIRE_FALSE(e->isSet());
                }(event)
            );

            REQUIRE(task.cancel());
            co_await task;
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

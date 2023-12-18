#include <asyncio/sync/condition.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asyncio condition variable", "[sync]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        const auto condition = std::make_shared<asyncio::sync::Condition>();
        const auto mutex = std::make_shared<asyncio::sync::Mutex>();
        REQUIRE(!mutex->locked());

        SECTION("notify") {
            co_await allSettled(
                [](auto c, auto m) -> zero::async::coroutine::Task<void> {
                    auto result = co_await m->lock();
                    REQUIRE(result);
                    REQUIRE(m->locked());

                    result = co_await c->wait(*m);
                    REQUIRE(result);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> zero::async::coroutine::Task<void> {
                    auto result = co_await m->lock();
                    REQUIRE(result);
                    REQUIRE(m->locked());

                    result = co_await c->wait(*m);
                    REQUIRE(result);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> zero::async::coroutine::Task<void> {
                    auto result = co_await m->lock();
                    REQUIRE(result);
                    REQUIRE(m->locked());

                    result = co_await c->wait(*m);
                    REQUIRE(result);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> zero::async::coroutine::Task<void> {
                    co_await asyncio::sleep(20ms);
                    const auto result = co_await m->lock();
                    REQUIRE(result);

                    c->notify();
                    c->notify();
                    c->notify();
                    m->unlock();
                }(condition, mutex)
            );
        }

        SECTION("broadcast") {
            co_await allSettled(
                [](auto c, auto m) -> zero::async::coroutine::Task<void> {
                    auto result = co_await m->lock();
                    REQUIRE(result);
                    REQUIRE(m->locked());

                    result = co_await c->wait(*m);
                    REQUIRE(result);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> zero::async::coroutine::Task<void> {
                    auto result = co_await m->lock();
                    REQUIRE(result);
                    REQUIRE(m->locked());

                    result = co_await c->wait(*m);
                    REQUIRE(result);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> zero::async::coroutine::Task<void> {
                    auto result = co_await m->lock();
                    REQUIRE(result);
                    REQUIRE(m->locked());

                    result = co_await c->wait(*m);
                    REQUIRE(result);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> zero::async::coroutine::Task<void> {
                    co_await asyncio::sleep(20ms);
                    const auto result = co_await m->lock();
                    REQUIRE(result);

                    c->broadcast();
                    m->unlock();
                }(condition, mutex)
            );
        }

        SECTION("predicate") {
            const auto value = std::make_shared<int>();

            co_await allSettled(
                [](auto c, auto m, auto v) -> zero::async::coroutine::Task<void> {
                    auto result = co_await m->lock();
                    REQUIRE(result);
                    REQUIRE(m->locked());

                    result = co_await c->wait(*m, [=] {
                        return *v == 1024;
                    });

                    REQUIRE(result);
                    REQUIRE(m->locked());
                    REQUIRE(*v == 1024);
                    m->unlock();
                }(condition, mutex, value),
                [](auto c, auto m, auto v) -> zero::async::coroutine::Task<void> {
                    co_await asyncio::sleep(20ms);
                    auto result = co_await m->lock();
                    REQUIRE(result);

                    *v = 1023;
                    c->notify();
                    m->unlock();

                    co_await asyncio::sleep(20ms);

                    result = co_await m->lock();
                    REQUIRE(result);

                    *v = 1024;
                    c->notify();
                    m->unlock();
                }(condition, mutex, value)
            );
        }

        SECTION("timeout") {
            co_await allSettled(
                [](auto c, auto m) -> zero::async::coroutine::Task<void> {
                    auto result = co_await m->lock();
                    REQUIRE(result);
                    REQUIRE(m->locked());

                    result = co_await c->wait(*m, 10ms);
                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::timed_out);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> zero::async::coroutine::Task<void> {
                    co_await asyncio::sleep(20ms);
                    const auto result = co_await m->lock();
                    REQUIRE(result);

                    c->notify();
                    m->unlock();
                }(condition, mutex)
            );
        }

        SECTION("cancel") {
            auto task = [](auto c, auto m) -> zero::async::coroutine::Task<void> {
                auto result = co_await m->lock();
                REQUIRE(result);
                REQUIRE(m->locked());

                result = co_await c->wait(*m);
                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::operation_canceled);
                REQUIRE(m->locked());
                m->unlock();
            }(condition, mutex);

            task.cancel();
            co_await task;
        }
    });
}

#include <asyncio/sync/condition.h>
#include <asyncio/time.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("asyncio condition variable", "[sync]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        const auto condition = std::make_shared<asyncio::sync::Condition>();
        const auto mutex = std::make_shared<asyncio::sync::Mutex>();
        REQUIRE(!mutex->locked());

        SECTION("notify") {
            co_await allSettled(
                [](auto c, auto m) -> asyncio::task::Task<void> {
                    auto res = co_await m->lock();
                    REQUIRE(res);
                    REQUIRE(m->locked());

                    res = co_await c->wait(*m);
                    REQUIRE(res);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> asyncio::task::Task<void> {
                    auto res = co_await m->lock();
                    REQUIRE(res);
                    REQUIRE(m->locked());

                    res = co_await c->wait(*m);
                    REQUIRE(res);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> asyncio::task::Task<void> {
                    auto res = co_await m->lock();
                    REQUIRE(res);
                    REQUIRE(m->locked());

                    res = co_await c->wait(*m);
                    REQUIRE(res);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> asyncio::task::Task<void> {
                    using namespace std::chrono_literals;

                    co_await asyncio::sleep(20ms);
                    const auto res = co_await m->lock();
                    REQUIRE(res);

                    c->notify();
                    c->notify();
                    c->notify();
                    m->unlock();
                }(condition, mutex)
            );
        }

        SECTION("broadcast") {
            co_await allSettled(
                [](auto c, auto m) -> asyncio::task::Task<void> {
                    auto res = co_await m->lock();
                    REQUIRE(res);
                    REQUIRE(m->locked());

                    res = co_await c->wait(*m);
                    REQUIRE(res);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> asyncio::task::Task<void> {
                    auto res = co_await m->lock();
                    REQUIRE(res);
                    REQUIRE(m->locked());

                    res = co_await c->wait(*m);
                    REQUIRE(res);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> asyncio::task::Task<void> {
                    auto res = co_await m->lock();
                    REQUIRE(res);
                    REQUIRE(m->locked());

                    res = co_await c->wait(*m);
                    REQUIRE(res);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> asyncio::task::Task<void> {
                    using namespace std::chrono_literals;

                    co_await asyncio::sleep(20ms);
                    const auto res = co_await m->lock();
                    REQUIRE(res);

                    c->broadcast();
                    m->unlock();
                }(condition, mutex)
            );
        }

        SECTION("predicate") {
            const auto value = std::make_shared<int>();

            co_await allSettled(
                [](auto c, auto m, auto v) -> asyncio::task::Task<void> {
                    auto res = co_await m->lock();
                    REQUIRE(res);
                    REQUIRE(m->locked());

                    res = co_await c->wait(*m, [=] {
                        return *v == 1024;
                    });

                    REQUIRE(res);
                    REQUIRE(m->locked());
                    REQUIRE(*v == 1024);
                    m->unlock();
                }(condition, mutex, value),
                [](auto c, auto m, auto v) -> asyncio::task::Task<void> {
                    using namespace std::chrono_literals;

                    co_await asyncio::sleep(20ms);
                    auto res = co_await m->lock();
                    REQUIRE(res);

                    *v = 1023;
                    c->notify();
                    m->unlock();

                    co_await asyncio::sleep(20ms);

                    res = co_await m->lock();
                    REQUIRE(res);

                    *v = 1024;
                    c->notify();
                    m->unlock();
                }(condition, mutex, value)
            );
        }

        SECTION("timeout") {
            co_await allSettled(
                [](auto c, auto m) -> asyncio::task::Task<void> {
                    using namespace std::chrono_literals;

                    const auto res = co_await m->lock();
                    REQUIRE(res);
                    REQUIRE(m->locked());

                    const auto r = co_await asyncio::timeout(c->wait(*m), 10ms);
                    REQUIRE(!r);
                    REQUIRE(r.error() == asyncio::TimeoutError::ELAPSED);
                    REQUIRE(m->locked());
                    m->unlock();
                }(condition, mutex),
                [](auto c, auto m) -> asyncio::task::Task<void> {
                    using namespace std::chrono_literals;

                    co_await asyncio::sleep(20ms);
                    const auto res = co_await m->lock();
                    REQUIRE(res);

                    c->notify();
                    m->unlock();
                }(condition, mutex)
            );
        }

        SECTION("cancel") {
            auto task = [](auto c, auto m) -> asyncio::task::Task<void> {
                auto res = co_await m->lock();
                REQUIRE(res);
                REQUIRE(m->locked());

                res = co_await c->wait(*m);
                REQUIRE(!res);
                REQUIRE(res.error() == std::errc::operation_canceled);
                REQUIRE(m->locked());
                m->unlock();
            }(condition, mutex);

            REQUIRE(task.cancel());
            co_await task;
        }

        SECTION("cancel after notify") {
            auto res = co_await mutex->lock();
            REQUIRE(res);
            REQUIRE(mutex->locked());

            auto task = condition->wait(*mutex);
            REQUIRE(!task.done());

            res = co_await mutex->lock();
            REQUIRE(res);
            REQUIRE(mutex->locked());

            condition->notify();

            res = task.cancel();
            REQUIRE(!res);
            REQUIRE(res.error() == asyncio::task::Error::WILL_BE_DONE);

            mutex->unlock();
            REQUIRE(!mutex->locked());

            res = co_await task;
            REQUIRE(res);
            REQUIRE(mutex->locked());
        }

        SECTION("notify after cancel") {
            auto res = co_await mutex->lock();
            REQUIRE(res);
            REQUIRE(mutex->locked());

            auto task1 = condition->wait(*mutex);
            REQUIRE(!task1.done());
            REQUIRE(task1.cancel());
            REQUIRE(!task1.done());

            res = co_await mutex->lock();
            REQUIRE(res);
            REQUIRE(mutex->locked());

            condition->notify();

            auto task2 = condition->wait(*mutex);
            REQUIRE(!task2.done());

            res = co_await task1;
            REQUIRE(!res);
            REQUIRE(res.error() == std::errc::operation_canceled);
            REQUIRE(mutex->locked());

            mutex->unlock();
            REQUIRE(!mutex->locked());

            res = co_await task2;
            REQUIRE(res);
            REQUIRE(mutex->locked());
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

#include <asyncio/future.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asyncio future", "[future]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("have result") {
            const asyncio::Future<int> future;
            REQUIRE(!future.done());

            SECTION("no error") {
                co_await allSettled(
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(result);
                        REQUIRE(*result == 1024);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(result);
                        REQUIRE(*result == 1024);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(result);
                        REQUIRE(*result == 1024);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        co_await asyncio::sleep(50ms);
                        f.set(1024);
                        REQUIRE(f.done());
                    }(future)
                );
            }

            SECTION("error") {
                co_await allSettled(
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::owner_dead);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::owner_dead);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::owner_dead);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        co_await asyncio::sleep(50ms);
                        f.setError(make_error_code(std::errc::owner_dead));
                        REQUIRE(f.done());
                    }(future)
                );
            }

            SECTION("timeout") {
                co_await allSettled(
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get(10ms);
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::timed_out);
                        REQUIRE(!f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get(100ms);
                        REQUIRE(result);
                        REQUIRE(*result == 1024);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(result);
                        REQUIRE(*result == 1024);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        co_await asyncio::sleep(50ms);
                        f.set(1024);
                        REQUIRE(f.done());
                    }(future)
                );
            }

            SECTION("cancel") {
                auto task = allSettled(
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::operation_canceled);
                        REQUIRE(!f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::operation_canceled);
                        REQUIRE(!f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::operation_canceled);
                        REQUIRE(!f.done());
                    }(future)
                );

                task.cancel();
                co_await task;
            }
        }

        SECTION("no result") {
            const asyncio::Future<void> future;
            REQUIRE(!future.done());

            SECTION("no error") {
                co_await allSettled(
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(result);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(result);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(result);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        co_await asyncio::sleep(50ms);
                        f.set();
                        REQUIRE(f.done());
                    }(future)
                );
            }

            SECTION("error") {
                co_await allSettled(
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::owner_dead);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::owner_dead);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::owner_dead);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        co_await asyncio::sleep(50ms);
                        f.setError(make_error_code(std::errc::owner_dead));
                        REQUIRE(f.done());
                    }(future)
                );
            }

            SECTION("timeout") {
                co_await allSettled(
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get(10ms);
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::timed_out);
                        REQUIRE(!f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get(100ms);
                        REQUIRE(result);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(result);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        co_await asyncio::sleep(50ms);
                        f.set();
                        REQUIRE(f.done());
                    }(future)
                );
            }

            SECTION("cancel") {
                auto task = allSettled(
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::operation_canceled);
                        REQUIRE(!f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::operation_canceled);
                        REQUIRE(!f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await f.get();
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::operation_canceled);
                        REQUIRE(!f.done());
                    }(future)
                );

                task.cancel();
                co_await task;
            }
        }
    });
}

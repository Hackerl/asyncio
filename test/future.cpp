#include <asyncio/future.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asyncio future", "[future]") {
    SECTION("have result") {
        SECTION("no error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                const asyncio::Future<int> future;
                REQUIRE(!future.done());

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
            });
        }

        SECTION("error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                const asyncio::Future<int> future;
                REQUIRE(!future.done());

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
            });
        }

        SECTION("timeout") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                const asyncio::Future<int> future;
                REQUIRE(!future.done());

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
            });
        }

        SECTION("cancel") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                const asyncio::Future<int> future;
                REQUIRE(!future.done());

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
            });
        }
    }

    SECTION("no result") {
        SECTION("no error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                const asyncio::Future<void> future;
                REQUIRE(!future.done());

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
            });
        }

        SECTION("error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                const asyncio::Future<void> future;
                REQUIRE(!future.done());

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
            });
        }

        SECTION("timeout") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                const asyncio::Future<void> future;
                REQUIRE(!future.done());

                co_await allSettled(
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        auto result = co_await f.get(10ms);
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::timed_out);
                        REQUIRE(!f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        auto result = co_await f.get(100ms);
                        REQUIRE(result);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        auto result = co_await f.get();
                        REQUIRE(result);
                        REQUIRE(f.done());
                    }(future),
                    [](auto f) -> zero::async::coroutine::Task<void> {
                        co_await asyncio::sleep(50ms);
                        f.set();
                        REQUIRE(f.done());
                    }(future)
                );
            });
        }

        SECTION("cancel") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                const asyncio::Future<void> future;
                REQUIRE(!future.done());

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
            });
        }
    }
}

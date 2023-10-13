#include <asyncio/future.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asyncio future", "[future]") {
    SECTION("have result") {
        SECTION("no error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                asyncio::Future<int> future;

                co_await zero::async::coroutine::allSettled(
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(result);
                            REQUIRE(*result == 1024);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(result);
                            REQUIRE(*result == 1024);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(result);
                            REQUIRE(*result == 1024);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            co_await asyncio::sleep(50ms);
                            future.set(1024);
                        }(future)
                );
            });
        }

        SECTION("error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                asyncio::Future<int> future;

                co_await zero::async::coroutine::allSettled(
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::owner_dead);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::owner_dead);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::owner_dead);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            co_await asyncio::sleep(50ms);
                            future.setError(make_error_code(std::errc::owner_dead));
                        }(future)
                );
            });
        }

        SECTION("timeout") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                asyncio::Future<int> future;

                co_await zero::async::coroutine::allSettled(
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get(10ms);
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::timed_out);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get(100ms);
                            REQUIRE(result);
                            REQUIRE(*result == 1024);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(result);
                            REQUIRE(*result == 1024);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            co_await asyncio::sleep(50ms);
                            future.set(1024);
                        }(future)
                );
            });
        }

        SECTION("cancel") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                asyncio::Future<int> future;

                auto task = zero::async::coroutine::allSettled(
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::operation_canceled);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::operation_canceled);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::operation_canceled);
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
                asyncio::Future<void> future;

                co_await zero::async::coroutine::allSettled(
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(result);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(result);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(result);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            co_await asyncio::sleep(50ms);
                            future.set();
                        }(future)
                );
            });
        }

        SECTION("error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                asyncio::Future<void> future;

                co_await zero::async::coroutine::allSettled(
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::owner_dead);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::owner_dead);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::owner_dead);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            co_await asyncio::sleep(50ms);
                            future.setError(make_error_code(std::errc::owner_dead));
                        }(future)
                );
            });
        }

        SECTION("timeout") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                asyncio::Future<void> future;

                co_await zero::async::coroutine::allSettled(
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get(10ms);
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::timed_out);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get(100ms);
                            REQUIRE(result);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(result);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            co_await asyncio::sleep(50ms);
                            future.set();
                        }(future)
                );
            });
        }

        SECTION("cancel") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                asyncio::Future<void> future;

                auto task = zero::async::coroutine::allSettled(
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::operation_canceled);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::operation_canceled);
                        }(future),
                        [](auto future) -> zero::async::coroutine::Task<void> {
                            auto result = co_await future.get();
                            REQUIRE(!result);
                            REQUIRE(result.error() == std::errc::operation_canceled);
                        }(future)
                );

                task.cancel();
                co_await task;
            });
        }
    }
}
#include <asyncio/promise.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asyncio event loop", "[event loop]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("sleep") {
            const auto tp = std::chrono::system_clock::now();
            co_await asyncio::sleep(50ms);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("timeout") {
            SECTION("success") {
                const auto result = co_await asyncio::timeout(asyncio::sleep(10ms), 20ms);
                REQUIRE(result);
            }

            SECTION("timeout") {
                const auto result = co_await asyncio::timeout(asyncio::sleep(20ms), 10ms);
                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::timed_out);
            }

            SECTION("failure") {
                auto task = asyncio::sleep(50ms);
                task.cancel();
                const auto result = co_await asyncio::timeout(std::move(task), 20ms);
                REQUIRE(result);
                REQUIRE(result.value().error() == std::errc::operation_canceled);
            }

            SECTION("cancel") {
                auto task = asyncio::timeout(asyncio::sleep(20ms), 20ms);
                task.cancel();
                const auto result = co_await task;
                REQUIRE(result);
                REQUIRE(!*result);
                REQUIRE(result->error() == std::errc::operation_canceled);
            }

            SECTION("cannot cancel") {
                const auto promise = std::make_shared<asyncio::Promise<void, std::error_code>>();
                auto task = asyncio::timeout(
                    from(zero::async::coroutine::Cancellable{
                        promise->getFuture(),
                        [=]() -> tl::expected<void, std::error_code> {
                            return tl::unexpected(make_error_code(std::errc::operation_not_supported));
                        }
                    }),
                    20ms
                );

                SECTION("success") {
                    const auto results = co_await allSettled(
                        std::move(task),
                        [](auto p) -> zero::async::coroutine::Task<void> {
                            co_await asyncio::sleep(20ms);
                            p->resolve();
                        }(promise)
                    );

                    const auto &result = std::get<0>(results);
                    REQUIRE(result);
                    REQUIRE(*result);
                }

                SECTION("failure") {
                    const auto results = co_await allSettled(
                        std::move(task),
                        [](auto p) -> zero::async::coroutine::Task<void> {
                            co_await asyncio::sleep(20ms);
                            p->reject(make_error_code(std::errc::invalid_argument));
                        }(promise)
                    );

                    const auto &result = std::get<0>(results);
                    REQUIRE(result);
                    REQUIRE(!*result);
                    REQUIRE(result->error() == std::errc::invalid_argument);
                }
            }
        }
    });

    SECTION("with error") {
        SECTION("success") {
            const auto result = asyncio::run([]() -> zero::async::coroutine::Task<int, std::error_code> {
                co_await asyncio::sleep(10ms);
                co_return 1024;
            });
            REQUIRE(result);
            REQUIRE(*result);
            REQUIRE(**result == 1024);
        }

        SECTION("failure") {
            const auto result = asyncio::run([]() -> zero::async::coroutine::Task<void, std::error_code> {
                co_await asyncio::sleep(10ms);
                co_return tl::unexpected(make_error_code(std::errc::invalid_argument));
            });
            REQUIRE(result);
            REQUIRE(!*result);
            REQUIRE(result->error() == std::errc::invalid_argument);
        }
    }

    SECTION("with exception") {
        SECTION("success") {
            const auto result = asyncio::run([]() -> zero::async::coroutine::Task<int> {
                co_await asyncio::sleep(10ms);
                co_return 1024;
            });
            REQUIRE(result);
            REQUIRE(*result);
            REQUIRE(**result == 1024);
        }

        SECTION("failure") {
            const auto result = asyncio::run([]() -> zero::async::coroutine::Task<void> {
                co_await asyncio::sleep(10ms);
                throw std::system_error(make_error_code(std::errc::invalid_argument));
            });
            REQUIRE(result);
            REQUIRE(!*result);

            try {
                std::rethrow_exception(result->error());
            }
            catch (const std::system_error &error) {
                REQUIRE(error.code() == std::errc::invalid_argument);
            }
        }
    }
}

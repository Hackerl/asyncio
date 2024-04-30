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
                REQUIRE(result.error() == asyncio::TimeoutError::ELAPSED);
            }

            SECTION("failure") {
                auto task = asyncio::sleep(50ms);
                REQUIRE(task.cancel());
                const auto result = co_await asyncio::timeout(std::move(task), 20ms);
                REQUIRE(result);
                REQUIRE(result.value().error() == std::errc::operation_canceled);
            }

            SECTION("cancel") {
                auto task = asyncio::timeout(asyncio::sleep(20ms), 20ms);
                REQUIRE(task.cancel());
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
                            return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);
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

        SECTION("to thread") {
            SECTION("no result") {
                SECTION("no error") {
                    const auto tp = std::chrono::system_clock::now();

                    co_await asyncio::toThread([]() -> tl::expected<void, std::error_code> {
                        std::this_thread::sleep_for(50ms);
                        return {};
                    });

                    REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
                }

                SECTION("error") {
                    const auto result = co_await asyncio::toThread([]() -> tl::expected<void, std::error_code> {
                        std::this_thread::sleep_for(10ms);
                        return tl::unexpected(make_error_code(std::errc::bad_message));
                    });

                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::bad_message);
                }
            }

            SECTION("have result") {
                SECTION("no error") {
                    const auto result = co_await asyncio::toThread([]() -> tl::expected<int, std::error_code> {
                        std::this_thread::sleep_for(10ms);
                        return 1024;
                    });

                    REQUIRE(result);
                    REQUIRE(*result == 1024);
                }

                SECTION("error") {
                    const auto result = co_await asyncio::toThread([]() -> tl::expected<int, std::error_code> {
                        std::this_thread::sleep_for(10ms);
                        return tl::unexpected(make_error_code(std::errc::bad_message));
                    });

                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::bad_message);
                }
            }

            SECTION("timeout") {
                const auto stop = std::make_shared<bool>();

                auto task = asyncio::toThread(
                    [=]() -> tl::expected<int, std::error_code> {
                        while (!*stop)
                            std::this_thread::sleep_for(10ms);

                        return tl::unexpected(zero::async::coroutine::Error::CANCELLED);
                    },
                    [=](std::thread::native_handle_type) -> tl::expected<void, std::error_code> {
                        *stop = true;
                        return {};
                    }
                );

                co_await asyncio::sleep(10ms);
                REQUIRE(!task.done());
                REQUIRE(task.cancel());

                const auto result = co_await task;
                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::operation_canceled);
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

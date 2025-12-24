#include "catch_extensions.h"
#include <asyncio/thread.h>
#include <catch2/matchers/catch_matchers_all.hpp>

ASYNC_TEST_CASE("post task to a new thread", "[thread]") {
    using namespace std::chrono_literals;

    const auto tp = std::chrono::system_clock::now();

    SECTION("void") {
        co_await asyncio::toThread([] {
            std::this_thread::sleep_for(50ms);
        });
        REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
    }

    SECTION("not void") {
        const auto result = co_await asyncio::toThread([] {
            std::this_thread::sleep_for(50ms);
            return 1024;
        });
        REQUIRE(result == 1024);
        REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
    }

    SECTION("exception") {
        REQUIRE_THROWS_MATCHES(
            co_await asyncio::toThread([] {
                std::this_thread::sleep_for(50ms);
                throw std::system_error{make_error_code(std::errc::invalid_argument)};
            }),
            std::system_error,
            Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                return error.code() == std::errc::invalid_argument;
            })
        );
        REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
    }
}

ASYNC_TEST_CASE("post cancellable task to a new thread - error", "[thread]") {
    const auto tp = std::chrono::system_clock::now();

    SECTION("void") {
        using namespace std::chrono_literals;

        zero::atomic::Event event;

        auto task = asyncio::toThread(
            [&]() -> std::expected<void, std::error_code> {
                if (event.wait(50ms))
                    return std::unexpected{asyncio::task::Error::CANCELLED};

                return {};
            },
            [&](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
                event.set();
                return {};
            }
        );

        SECTION("normal") {
            REQUIRE(co_await task);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
            REQUIRE(std::chrono::system_clock::now() - tp < 50ms);
        }
    }

    SECTION("not void") {
        using namespace std::chrono_literals;

        zero::atomic::Event event;

        auto task = asyncio::toThread(
            [&]() -> std::expected<int, std::error_code> {
                if (event.wait(50ms))
                    return std::unexpected{asyncio::task::Error::CANCELLED};

                return 1024;
            },
            [&](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
                event.set();
                return {};
            }
        );

        SECTION("normal") {
            REQUIRE(co_await task == 1024);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
            REQUIRE(std::chrono::system_clock::now() - tp < 50ms);
        }
    }
}

ASYNC_TEST_CASE("post cancellable task to a new thread - exception", "[thread]") {
    const auto tp = std::chrono::system_clock::now();

    SECTION("void") {
        using namespace std::chrono_literals;

        zero::atomic::Event event;

        auto task = asyncio::toThread(
            [&] {
                if (event.wait(50ms))
                    throw std::system_error{asyncio::task::Error::CANCELLED};
            },
            [&](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
                event.set();
                return {};
            }
        );

        SECTION("normal") {
            REQUIRE_NOTHROW(co_await task);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());
            REQUIRE_THROWS_MATCHES(
                co_await task,
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::operation_canceled;
                })
            );
            REQUIRE(std::chrono::system_clock::now() - tp < 50ms);
        }
    }

    SECTION("not void") {
        using namespace std::chrono_literals;

        zero::atomic::Event event;

        auto task = asyncio::toThread(
            [&] {
                if (event.wait(50ms))
                    throw std::system_error{asyncio::task::Error::CANCELLED};

                return 1024;
            },
            [&](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
                event.set();
                return {};
            }
        );

        SECTION("normal") {
            REQUIRE(co_await task == 1024);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());
            REQUIRE_THROWS_MATCHES(
                co_await task,
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::operation_canceled;
                })
            );
            REQUIRE(std::chrono::system_clock::now() - tp < 50ms);
        }
    }
}

ASYNC_TEST_CASE("post task to thread pool", "[thread]") {
    using namespace std::chrono_literals;

    const auto tp = std::chrono::system_clock::now();

    SECTION("normal") {
        SECTION("void") {
            const auto result = co_await asyncio::toThreadPool([] {
                std::this_thread::sleep_for(50ms);
            });
            REQUIRE(result);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("not void") {
            const auto result = co_await asyncio::toThreadPool([] {
                std::this_thread::sleep_for(50ms);
                return 1024;
            });
            REQUIRE(result == 1024);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }
    }

    SECTION("cancel") {
        std::list<asyncio::task::Task<void, asyncio::ToThreadPoolError>> tasks;

        // The default size of the `libuv` thread pool is 4.
        for (int i{0}; i < 4; ++i) {
            tasks.push_back(asyncio::toThreadPool([] {
                std::this_thread::sleep_for(1s);
            }));
        }

        SECTION("void") {
            auto task = asyncio::toThreadPool([] {
                std::this_thread::sleep_for(50ms);
            });
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, asyncio::ToThreadPoolError::CANCELLED);
            REQUIRE(std::chrono::system_clock::now() - tp < 50ms);
        }

        SECTION("not void") {
            auto task = asyncio::toThreadPool([] {
                std::this_thread::sleep_for(50ms);
                return 1024;
            });
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, asyncio::ToThreadPoolError::CANCELLED);
            REQUIRE(std::chrono::system_clock::now() - tp < 50ms);
        }

        REQUIRE(co_await all(tasks));
    }
}

ASYNC_TEST_CASE("post cancellable task to thread pool", "[thread]") {
    const auto tp = std::chrono::system_clock::now();

    SECTION("void") {
        using namespace std::chrono_literals;

        std::array<zero::atomic::Event, 2> events;

        auto task = asyncio::toThreadPool(
            [&]() -> std::expected<void, std::error_code> {
                events[0].set();

                if (events[1].wait(50ms))
                    return std::unexpected{asyncio::task::Error::CANCELLED};

                return {};
            },
            [&]() -> std::expected<void, std::error_code> {
                events[1].set();
                return {};
            }
        );

        REQUIRE(events[0].wait());

        SECTION("normal") {
            REQUIRE(co_await task);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());

            const auto result = co_await task;
            REQUIRE(result);
            REQUIRE_ERROR(*result, std::errc::operation_canceled);
            REQUIRE(std::chrono::system_clock::now() - tp < 50ms);
        }
    }

    SECTION("not void") {
        using namespace std::chrono_literals;

        std::array<zero::atomic::Event, 2> events;

        auto task = asyncio::toThreadPool(
            [&]() -> std::expected<int, std::error_code> {
                events[0].set();

                if (events[1].wait(50ms))
                    return std::unexpected{asyncio::task::Error::CANCELLED};

                return 1024;
            },
            [&]() -> std::expected<void, std::error_code> {
                events[1].set();
                return {};
            }
        );

        REQUIRE(events[0].wait());

        SECTION("normal") {
            REQUIRE(co_await task == 1024);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());

            const auto result = co_await task;
            REQUIRE(result);
            REQUIRE_ERROR(*result, std::errc::operation_canceled);
            REQUIRE(std::chrono::system_clock::now() - tp < 50ms);
        }
    }
}

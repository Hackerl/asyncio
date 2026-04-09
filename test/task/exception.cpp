#include <catch_extensions.h>
#include <asyncio/task.h>
#include <catch2/matchers/catch_matchers_all.hpp>

ASYNC_TEST_CASE("cancellable task - exception", "[task]") {
    asyncio::Promise<void> promise;
    auto task = from(asyncio::task::Cancellable{
        asyncio::task::from(promise.getFuture()),
        [&]() -> std::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return std::unexpected{asyncio::task::Error::CancellationTooLate};

            promise.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
            return {};
        }
    });
    REQUIRE(task.cancel());
    REQUIRE_THROWS_MATCHES(
        co_await task,
        std::system_error,
        Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
            return error.code() == asyncio::task::Error::Cancelled;
        })
    );
}

ASYNC_TEST_CASE("cancel task - exception", "[task]") {
    SECTION("success") {
        asyncio::Promise<void> promise;
        auto task = from(asyncio::task::Cancellable{
            promise.getFuture(),
            [&]() -> std::expected<void, std::error_code> {
                if (promise.isFulfilled())
                    return std::unexpected{asyncio::task::Error::CancellationTooLate};

                promise.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                return {};
            }
        });
        REQUIRE_FALSE(task.cancelled());
        REQUIRE(task.cancel());
        REQUIRE(task.cancelled());
        REQUIRE_THROWS_MATCHES(
            co_await task,
            std::system_error,
            Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                return error.code() == asyncio::task::Error::Cancelled;
            })
        );
    }

    SECTION("failure") {
        asyncio::Promise<void> promise;
        auto task = asyncio::task::from(promise.getFuture());

        REQUIRE_FALSE(task.cancelled());
        REQUIRE_ERROR(task.cancel(), asyncio::task::Error::CancellationNotSupported);
        REQUIRE(task.cancelled());

        promise.resolve();
        REQUIRE_NOTHROW(co_await task);
    }
}

ASYNC_TEST_CASE("automatically cancel at next suspension point - exception", "[task]") {
    asyncio::Promise<void> promise1;
    asyncio::Promise<void> promise2;

    auto task = asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
        co_await promise1.getFuture();
        co_await from(asyncio::task::Cancellable{
            promise2.getFuture(),
            [&]() -> std::expected<void, std::error_code> {
                promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                return {};
            }
        });
    });
    REQUIRE_ERROR(task.cancel(), asyncio::task::Error::CancellationNotSupported);

    promise1.resolve();

    REQUIRE_THROWS_MATCHES(
        co_await task,
        std::system_error,
        Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
            return error.code() == asyncio::task::Error::Cancelled;
        })
    );
}

ASYNC_TEST_CASE("check if the current task has been cancelled - exception", "[task]") {
    asyncio::Promise<void> promise;

    auto task = asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
        REQUIRE_FALSE(co_await asyncio::task::cancelled);

        REQUIRE_THROWS_MATCHES(
            (co_await asyncio::task::Cancellable{
                promise.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    promise.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            std::system_error,
            Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                return error.code() == std::errc::operation_canceled;
            })
        );

        REQUIRE(co_await asyncio::task::cancelled);
    });

    REQUIRE(task.cancel());
    co_await task;
}

ASYNC_TEST_CASE("lock task - exception", "[task]") {
    asyncio::Promise<void> promise;

    auto task = asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
        REQUIRE_FALSE(co_await asyncio::task::cancelled);
        co_await asyncio::task::lock;

        co_await asyncio::task::Cancellable{
            promise.getFuture(),
            [&]() -> std::expected<void, std::error_code> {
                promise.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                return {};
            }
        };

        co_await asyncio::task::unlock;
        REQUIRE(co_await asyncio::task::cancelled);
    });
    REQUIRE_ERROR(task.cancel(), asyncio::task::Error::Locked);

    promise.resolve();
    REQUIRE_NOTHROW(co_await task);
}

ASYNC_TEST_CASE("task trace - exception", "[task]") {
    asyncio::Promise<void> promise;
    auto task = asyncio::task::from(promise.getFuture());
    REQUIRE_THAT(task.trace(), Catch::Matchers::ContainsSubstring("from"));

    promise.resolve();
    REQUIRE_NOTHROW(co_await task);
    REQUIRE_THAT(task.trace(), Catch::Matchers::IsEmpty());
}

ASYNC_TEST_CASE("task call tree - exception", "[task]") {
    asyncio::Promise<void> promise;
    auto task = asyncio::task::from(promise.getFuture());
    REQUIRE_THAT(
        task.callTree(),
        Catch::Matchers::Contains(
            Catch::Matchers::Predicate<std::source_location>([](const auto &location) {
                return std::string_view{location.function_name()}.contains("from");
            })
        )
    );

    promise.resolve();
    REQUIRE_NOTHROW(co_await task);
    REQUIRE_THAT(task.callTree(), Catch::Matchers::IsEmpty());
}

ASYNC_TEST_CASE("task all - exception", "[task]") {
    SECTION("void") {
        asyncio::Promise<void> promise1;
        asyncio::Promise<void> promise2;

        auto task = all(std::array{
            from(asyncio::task::Cancellable{
                promise1.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise2.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            })
        });

        SECTION("success") {
            promise1.resolve();
            promise2.resolve();
            REQUIRE_NOTHROW(co_await task);
        }

        SECTION("failure") {
            promise1.resolve();
            promise2.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));

            REQUIRE_THROWS_MATCHES(
                co_await task,
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );
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
        }
    }

    SECTION("not void") {
        asyncio::Promise<int> promise1;
        asyncio::Promise<int> promise2;

        auto task = all(std::array{
            from(asyncio::task::Cancellable{
                promise1.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise2.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            })
        });

        SECTION("success") {
            promise1.resolve(1);
            promise2.resolve(2);

            const auto result = co_await task;
            REQUIRE(result[0] == 1);
            REQUIRE(result[1] == 2);
        }

        SECTION("failure") {
            promise1.resolve(1);
            promise2.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));

            REQUIRE_THROWS_MATCHES(
                co_await task,
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );
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
        }
    }
}

ASYNC_TEST_CASE("task variadic all - exception", "[task]") {
    SECTION("same types") {
        SECTION("void") {
            asyncio::Promise<void> promise1;
            asyncio::Promise<void> promise2;

            auto task = all(
                from(asyncio::task::Cancellable{
                    promise1.getFuture(),
                    [&]() -> std::expected<void, std::error_code> {
                        if (promise1.isFulfilled())
                            return std::unexpected{asyncio::task::Error::CancellationTooLate};

                        promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                        return {};
                    }
                }),
                from(asyncio::task::Cancellable{
                    promise2.getFuture(),
                    [&]() -> std::expected<void, std::error_code> {
                        if (promise2.isFulfilled())
                            return std::unexpected{asyncio::task::Error::CancellationTooLate};

                        promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                        return {};
                    }
                })
            );

            SECTION("success") {
                promise1.resolve();
                promise2.resolve();
                REQUIRE_NOTHROW(co_await task);
            }

            SECTION("failure") {
                promise1.resolve();
                promise2.reject(
                    std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)})
                );

                REQUIRE_THROWS_MATCHES(
                    co_await task,
                    std::system_error,
                    Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                        return error.code() == std::errc::invalid_argument;
                    })
                );
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
            }
        }

        SECTION("not void") {
            asyncio::Promise<int> promise1;
            asyncio::Promise<int> promise2;

            auto task = all(
                from(asyncio::task::Cancellable{
                    promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise1.isFulfilled())
                            return std::unexpected{asyncio::task::Error::CancellationTooLate};

                        promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                        return {};
                    }
                }),
                from(asyncio::task::Cancellable{
                    promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise2.isFulfilled())
                            return std::unexpected{asyncio::task::Error::CancellationTooLate};

                        promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                        return {};
                    }
                })
            );

            SECTION("success") {
                promise1.resolve(1);
                promise2.resolve(2);

                const auto result = co_await task;
                REQUIRE(result[0] == 1);
                REQUIRE(result[1] == 2);
            }

            SECTION("failure") {
                promise1.resolve(0);
                promise2.reject(
                    std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)})
                );

                REQUIRE_THROWS_MATCHES(
                    co_await task,
                    std::system_error,
                    Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                        return error.code() == std::errc::invalid_argument;
                    })
                );
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
            }
        }
    }

    SECTION("different types") {
        asyncio::Promise<int> promise1;
        asyncio::Promise<void> promise2;
        asyncio::Promise<long> promise3;

        auto task = all(
            from(asyncio::task::Cancellable{
                promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise3.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise3.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise3.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            })
        );

        SECTION("success") {
            promise1.resolve(1);
            promise2.resolve();
            promise3.resolve(2);

            const auto result = co_await task;
            REQUIRE(std::get<0>(result) == 1);
            REQUIRE(std::get<2>(result) == 2);
        }

        SECTION("failure") {
            promise1.resolve(1);
            promise2.resolve();
            promise3.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));

            REQUIRE_THROWS_MATCHES(
                co_await task,
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );
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
        }
    }
}

ASYNC_TEST_CASE("task allSettled - exception", "[task]") {
    SECTION("void") {
        asyncio::Promise<void> promise1;
        asyncio::Promise<void> promise2;

        auto task = allSettled(std::array{
            from(asyncio::task::Cancellable{
                promise1.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise2.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            })
        });

        SECTION("normal") {
            promise1.resolve();
            promise2.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));

            const auto result = co_await task;
            REQUIRE(result[0]);
            REQUIRE_FALSE(result[1]);

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result[1].error()),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());

            const auto result = co_await task;
            REQUIRE_FALSE(result[0]);
            REQUIRE_FALSE(result[1]);

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result[0].error()),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::operation_canceled;
                })
            );

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result[1].error()),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::operation_canceled;
                })
            );
        }
    }

    SECTION("not void") {
        asyncio::Promise<int> promise1;
        asyncio::Promise<int> promise2;

        auto task = allSettled(std::array{
            from(asyncio::task::Cancellable{
                promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            })
        });

        SECTION("normal") {
            promise1.resolve(1);
            promise2.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));

            const auto result = co_await task;
            REQUIRE(result[0] == 1);
            REQUIRE_FALSE(result[1]);

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result[1].error()),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());

            const auto result = co_await task;
            REQUIRE_FALSE(result[0]);
            REQUIRE_FALSE(result[1]);

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result[0].error()),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::operation_canceled;
                })
            );

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result[1].error()),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::operation_canceled;
                })
            );
        }
    }
}

ASYNC_TEST_CASE("task variadic allSettled - exception", "[task]") {
    asyncio::Promise<int> promise1;
    asyncio::Promise<void> promise2;
    asyncio::Promise<long> promise3;

    auto task = allSettled(
        from(asyncio::task::Cancellable{
            promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                if (promise1.isFulfilled())
                    return std::unexpected{asyncio::task::Error::CancellationTooLate};

                promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                return {};
            }
        }),
        from(asyncio::task::Cancellable{
            promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                if (promise2.isFulfilled())
                    return std::unexpected{asyncio::task::Error::CancellationTooLate};

                promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                return {};
            }
        }),
        from(asyncio::task::Cancellable{
            promise3.getFuture(), [&]() -> std::expected<void, std::error_code> {
                if (promise3.isFulfilled())
                    return std::unexpected{asyncio::task::Error::CancellationTooLate};

                promise3.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                return {};
            }
        })
    );

    SECTION("normal") {
        promise1.resolve(1);
        promise2.resolve();
        promise3.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));

        const auto result = co_await task;
        REQUIRE(std::get<0>(result) == 1);
        REQUIRE(std::get<1>(result));
        REQUIRE_FALSE(std::get<2>(result));

        REQUIRE_THROWS_MATCHES(
            std::rethrow_exception(std::get<2>(result).error()),
            std::system_error,
            Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                return error.code() == std::errc::invalid_argument;
            })
        );
    }

    SECTION("cancel") {
        REQUIRE(task.cancel());

        const auto result = co_await task;
        REQUIRE_FALSE(std::get<0>(result));
        REQUIRE_FALSE(std::get<1>(result));
        REQUIRE_FALSE(std::get<2>(result));

        REQUIRE_THROWS_MATCHES(
            std::rethrow_exception(std::get<0>(result).error()),
            std::system_error,
            Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                return error.code() == std::errc::operation_canceled;
            })
        );

        REQUIRE_THROWS_MATCHES(
            std::rethrow_exception(std::get<1>(result).error()),
            std::system_error,
            Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                return error.code() == std::errc::operation_canceled;
            })
        );

        REQUIRE_THROWS_MATCHES(
            std::rethrow_exception(std::get<2>(result).error()),
            std::system_error,
            Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                return error.code() == std::errc::operation_canceled;
            })
        );
    }
}

ASYNC_TEST_CASE("task any - exception", "[task]") {
    SECTION("void") {
        asyncio::Promise<void> promise1;
        asyncio::Promise<void> promise2;

        auto task = any(std::array{
            from(asyncio::task::Cancellable{
                promise1.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise2.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            })
        });

        SECTION("success") {
            promise1.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));
            promise2.resolve();
            REQUIRE_NOTHROW(co_await task);
        }

        SECTION("failure") {
            promise1.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));
            promise2.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::io_error)}));

            const auto result = co_await task;
            REQUIRE_FALSE(result);

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[0]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[1]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::io_error;
                })
            );
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());

            const auto result = co_await task;
            REQUIRE_FALSE(result);

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[0]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::operation_canceled;
                })
            );

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[1]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::operation_canceled;
                })
            );
        }
    }

    SECTION("not void") {
        asyncio::Promise<int> promise1;
        asyncio::Promise<int> promise2;

        auto task = any(std::array{
            from(asyncio::task::Cancellable{
                promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            })
        });

        SECTION("success") {
            promise1.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));
            promise2.resolve(1);
            REQUIRE(co_await task == 1);
        }

        SECTION("failure") {
            promise1.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));
            promise2.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::io_error)}));

            const auto result = co_await task;
            REQUIRE_FALSE(result);

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[0]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[1]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::io_error;
                })
            );
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());

            const auto result = co_await task;
            REQUIRE_FALSE(result);

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[0]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::operation_canceled;
                })
            );

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[1]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::operation_canceled;
                })
            );
        }
    }
}

ASYNC_TEST_CASE("task variadic any - exception", "[task]") {
    SECTION("same types") {
        SECTION("void") {
            asyncio::Promise<void> promise1;
            asyncio::Promise<void> promise2;

            auto task = any(
                from(asyncio::task::Cancellable{
                    promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise1.isFulfilled())
                            return std::unexpected{asyncio::task::Error::CancellationTooLate};

                        promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                        return {};
                    }
                }),
                from(asyncio::task::Cancellable{
                    promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise2.isFulfilled())
                            return std::unexpected{asyncio::task::Error::CancellationTooLate};

                        promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                        return {};
                    }
                })
            );

            SECTION("success") {
                promise1.reject(
                    std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)})
                );
                promise2.resolve();
                REQUIRE_NOTHROW(co_await task);
            }

            SECTION("failure") {
                promise1.reject(
                    std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)})
                );
                promise2.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::io_error)}));

                const auto result = co_await task;
                REQUIRE_FALSE(result);

                REQUIRE_THROWS_MATCHES(
                    std::rethrow_exception(result.error()[0]),
                    std::system_error,
                    Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                        return error.code() == std::errc::invalid_argument;
                    })
                );

                REQUIRE_THROWS_MATCHES(
                    std::rethrow_exception(result.error()[1]),
                    std::system_error,
                    Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                        return error.code() == std::errc::io_error;
                    })
                );
            }

            SECTION("cancel") {
                REQUIRE(task.cancel());

                const auto result = co_await task;
                REQUIRE_FALSE(result);

                REQUIRE_THROWS_MATCHES(
                    std::rethrow_exception(result.error()[0]),
                    std::system_error,
                    Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                        return error.code() == std::errc::operation_canceled;
                    })
                );

                REQUIRE_THROWS_MATCHES(
                    std::rethrow_exception(result.error()[1]),
                    std::system_error,
                    Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                        return error.code() == std::errc::operation_canceled;
                    })
                );
            }
        }

        SECTION("not void") {
            asyncio::Promise<int> promise1;
            asyncio::Promise<int> promise2;

            auto task = any(
                from(asyncio::task::Cancellable{
                    promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise1.isFulfilled())
                            return std::unexpected{asyncio::task::Error::CancellationTooLate};

                        promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                        return {};
                    }
                }),
                from(asyncio::task::Cancellable{
                    promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise2.isFulfilled())
                            return std::unexpected{asyncio::task::Error::CancellationTooLate};

                        promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                        return {};
                    }
                })
            );

            SECTION("success") {
                promise1.reject(
                    std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)})
                );
                promise2.resolve(1);
                REQUIRE(co_await task == 1);
            }

            SECTION("failure") {
                promise1.reject(
                    std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)})
                );
                promise2.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::io_error)}));

                const auto result = co_await task;
                REQUIRE_FALSE(result);

                REQUIRE_THROWS_MATCHES(
                    std::rethrow_exception(result.error()[0]),
                    std::system_error,
                    Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                        return error.code() == std::errc::invalid_argument;
                    })
                );

                REQUIRE_THROWS_MATCHES(
                    std::rethrow_exception(result.error()[1]),
                    std::system_error,
                    Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                        return error.code() == std::errc::io_error;
                    })
                );
            }

            SECTION("cancel") {
                REQUIRE(task.cancel());

                const auto result = co_await task;
                REQUIRE_FALSE(result);

                REQUIRE_THROWS_MATCHES(
                    std::rethrow_exception(result.error()[0]),
                    std::system_error,
                    Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                        return error.code() == std::errc::operation_canceled;
                    })
                );

                REQUIRE_THROWS_MATCHES(
                    std::rethrow_exception(result.error()[1]),
                    std::system_error,
                    Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                        return error.code() == std::errc::operation_canceled;
                    })
                );
            }
        }
    }

#if !defined(_LIBCPP_VERSION) || _LIBCPP_VERSION >= 190000
    SECTION("different types") {
        asyncio::Promise<int> promise1;
        asyncio::Promise<void> promise2;
        asyncio::Promise<long> promise3;

        auto task = any(
            from(asyncio::task::Cancellable{
                promise1.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise2.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise3.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise3.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise3.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            })
        );

        SECTION("success") {
            SECTION("no value") {
                promise1.reject(
                    std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)})
                );
                promise2.resolve();

                const auto result = co_await task;
                REQUIRE(result);
                REQUIRE_FALSE(result->has_value());
            }

            SECTION("has value") {
                promise1.reject(
                    std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)})
                );
                promise2.reject(
                    std::make_exception_ptr(std::system_error{make_error_code(std::errc::io_error)})
                );
                promise3.resolve(1);

                const auto result = co_await task;
                REQUIRE(result);
                REQUIRE(result->has_value());

#if defined(_CPPRTTI) || defined(__GXX_RTTI)
                REQUIRE(result->type() == typeid(long));
#endif
                REQUIRE(std::any_cast<long>(*result) == 1);
            }
        }

        SECTION("failure") {
            promise1.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));
            promise2.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::io_error)}));
            promise3.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::bad_message)}));

            const auto result = co_await task;
            REQUIRE_FALSE(result);

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[0]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[1]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::io_error;
                })
            );

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[2]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::bad_message;
                })
            );
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());

            const auto result = co_await task;
            REQUIRE_FALSE(result);

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[0]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::operation_canceled;
                })
            );

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[1]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::operation_canceled;
                })
            );

            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()[2]),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::operation_canceled;
                })
            );
        }
    }
#endif
}

ASYNC_TEST_CASE("task race - exception", "[task]") {
    SECTION("void") {
        asyncio::Promise<void> promise1;
        asyncio::Promise<void> promise2;

        auto task = race(std::array{
            from(asyncio::task::Cancellable{
                promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            })
        });

        SECTION("success") {
            promise1.resolve();
            REQUIRE_NOTHROW(co_await task);
        }

        SECTION("failure") {
            promise1.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));
            REQUIRE_THROWS_MATCHES(
                co_await task,
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );
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
        }
    }

    SECTION("not void") {
        asyncio::Promise<int> promise1;
        asyncio::Promise<int> promise2;

        auto task = race(std::array{
            from(asyncio::task::Cancellable{
                promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            })
        });

        SECTION("success") {
            promise1.resolve(1);
            REQUIRE(co_await task == 1);
        }

        SECTION("failure") {
            promise1.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));
            REQUIRE_THROWS_MATCHES(
                co_await task,
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );
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
        }
    }
}

ASYNC_TEST_CASE("task variadic race - exception", "[task]") {
    SECTION("same types") {
        SECTION("void") {
            asyncio::Promise<void> promise1;
            asyncio::Promise<void> promise2;

            auto task = race(
                from(asyncio::task::Cancellable{
                    promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise1.isFulfilled())
                            return std::unexpected{asyncio::task::Error::CancellationTooLate};

                        promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                        return {};
                    }
                }),
                from(asyncio::task::Cancellable{
                    promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise2.isFulfilled())
                            return std::unexpected{asyncio::task::Error::CancellationTooLate};

                        promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                        return {};
                    }
                })
            );

            SECTION("success") {
                promise1.resolve();
                REQUIRE_NOTHROW(co_await task);
            }

            SECTION("failure") {
                promise1.reject(
                    std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)})
                );

                REQUIRE_THROWS_MATCHES(
                    co_await task,
                    std::system_error,
                    Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                        return error.code() == std::errc::invalid_argument;
                    })
                );
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
            }
        }

        SECTION("not void") {
            asyncio::Promise<int> promise1;
            asyncio::Promise<int> promise2;

            auto task = race(
                from(asyncio::task::Cancellable{
                    promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise1.isFulfilled())
                            return std::unexpected{asyncio::task::Error::CancellationTooLate};

                        promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                        return {};
                    }
                }),
                from(asyncio::task::Cancellable{
                    promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise2.isFulfilled())
                            return std::unexpected{asyncio::task::Error::CancellationTooLate};

                        promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                        return {};
                    }
                })
            );

            SECTION("success") {
                promise1.resolve(1);
                REQUIRE(co_await task == 1);
            }

            SECTION("failure") {
                promise1.reject(
                    std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)})
                );

                REQUIRE_THROWS_MATCHES(
                    co_await task,
                    std::system_error,
                    Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                        return error.code() == std::errc::invalid_argument;
                    })
                );
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
            }
        }
    }

#if !defined(_LIBCPP_VERSION) || _LIBCPP_VERSION >= 190000
    SECTION("different types") {
        asyncio::Promise<int> promise1;
        asyncio::Promise<void> promise2;
        asyncio::Promise<long> promise3;

        auto task = race(
            from(asyncio::task::Cancellable{
                promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise1.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise2.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            }),
            from(asyncio::task::Cancellable{
                promise3.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise3.isFulfilled())
                        return std::unexpected{asyncio::task::Error::CancellationTooLate};

                    promise3.reject(std::make_exception_ptr(std::system_error{asyncio::task::Error::Cancelled}));
                    return {};
                }
            })
        );

        SECTION("success") {
            SECTION("no value") {
                promise2.resolve();
                REQUIRE_FALSE((co_await task).has_value());
            }

            SECTION("has value") {
                promise1.resolve(1);

                const auto result = co_await task;
                REQUIRE(result.has_value());

#if defined(_CPPRTTI) || defined(__GXX_RTTI)
                REQUIRE(result.type() == typeid(int));
#endif
                REQUIRE(std::any_cast<int>(result) == 1);
            }
        }

        SECTION("failure") {
            promise1.reject(std::make_exception_ptr(std::system_error{make_error_code(std::errc::invalid_argument)}));
            REQUIRE_THROWS_MATCHES(
                co_await task,
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );
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
        }
    }
#endif
}

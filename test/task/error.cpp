#include <catch_extensions.h>
#include <asyncio/task.h>
#include <catch2/matchers/catch_matchers_all.hpp>

ASYNC_TEST_CASE("cancellable task - error", "[task]") {
    asyncio::Promise<void, std::error_code> promise;
    auto task = from(asyncio::task::CancellableTask{
        asyncio::task::from(promise.getFuture()),
        [&]() -> std::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

            promise.reject(asyncio::task::Error::CANCELLED);
            return {};
        }
    });
    REQUIRE(task.cancel());
    REQUIRE_ERROR(co_await task, asyncio::task::Error::CANCELLED);
}

ASYNC_TEST_CASE("cancel task - error", "[task]") {
    SECTION("success") {
        asyncio::Promise<void, std::error_code> promise;
        auto task = from(asyncio::task::CancellableFuture{
            promise.getFuture(),
            [&]() -> std::expected<void, std::error_code> {
                if (promise.isFulfilled())
                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                promise.reject(asyncio::task::Error::CANCELLED);
                return {};
            }
        });
        REQUIRE_FALSE(task.cancelled());
        REQUIRE(task.cancel());
        REQUIRE(task.cancelled());
        REQUIRE_ERROR(co_await task, asyncio::task::Error::CANCELLED);
    }

    SECTION("failure") {
        asyncio::Promise<void, std::error_code> promise;
        auto task = asyncio::task::from(promise.getFuture());

        REQUIRE_FALSE(task.cancelled());
        REQUIRE_ERROR(task.cancel(), asyncio::task::Error::CANCELLATION_NOT_SUPPORTED);
        REQUIRE(task.cancelled());

        promise.resolve();
        REQUIRE(co_await task);
    }
}

ASYNC_TEST_CASE("automatically cancel at next suspension point - error", "[task]") {
    asyncio::Promise<void, std::error_code> promise1;
    asyncio::Promise<void, std::error_code> promise2;

    auto task = asyncio::task::spawn([&]() -> asyncio::task::Task<void, std::error_code> {
        CO_EXPECT(co_await promise1.getFuture());
        co_return co_await asyncio::task::CancellableFuture{
            promise2.getFuture(),
            [&]() -> std::expected<void, std::error_code> {
                promise2.reject(asyncio::task::Error::CANCELLED);
                return {};
            }
        };
    });
    REQUIRE_ERROR(task.cancel(), asyncio::task::Error::CANCELLATION_NOT_SUPPORTED);

    promise1.resolve();
    REQUIRE_ERROR(co_await task, asyncio::task::Error::CANCELLED);
}

ASYNC_TEST_CASE("check if the current task has been cancelled - error", "[task]") {
    asyncio::Promise<void, std::error_code> promise;

    auto task = asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
        REQUIRE_FALSE(co_await asyncio::task::cancelled);

        const auto result = co_await asyncio::task::CancellableFuture{
            promise.getFuture(),
            [&]() -> std::expected<void, std::error_code> {
                promise.reject(asyncio::task::Error::CANCELLED);
                return {};
            }
        };
        REQUIRE_FALSE(result);
        REQUIRE_ERROR(result, std::errc::operation_canceled);
        REQUIRE(co_await asyncio::task::cancelled);
    });

    REQUIRE(task.cancel());
    co_await task;
}

ASYNC_TEST_CASE("lock task - error", "[task]") {
    asyncio::Promise<void, std::error_code> promise;

    auto task = asyncio::task::spawn([&]() -> asyncio::task::Task<void, std::error_code> {
        REQUIRE_FALSE(co_await asyncio::task::cancelled);
        co_await asyncio::task::lock;

        const auto result = co_await asyncio::task::CancellableFuture{
            promise.getFuture(),
            [&]() -> std::expected<void, std::error_code> {
                promise.reject(asyncio::task::Error::CANCELLED);
                return {};
            }
        };

        co_await asyncio::task::unlock;
        REQUIRE(co_await asyncio::task::cancelled);
        co_return result;
    });
    REQUIRE_ERROR(task.cancel(), asyncio::task::Error::LOCKED);

    promise.resolve();
    REQUIRE(co_await task);
}

ASYNC_TEST_CASE("task trace - error", "[task]") {
    asyncio::Promise<void, std::error_code> promise;
    auto task = asyncio::task::from(promise.getFuture());
    REQUIRE_THAT(task.trace(), Catch::Matchers::ContainsSubstring("from"));

    promise.resolve();
    REQUIRE(co_await task);
    REQUIRE_THAT(task.trace(), Catch::Matchers::IsEmpty());
}

ASYNC_TEST_CASE("task call tree - error", "[task]") {
    asyncio::Promise<void, std::error_code> promise;
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
    REQUIRE(co_await task);
    REQUIRE_THAT(task.callTree(), Catch::Matchers::IsEmpty());
}

ASYNC_TEST_CASE("task all - error", "[task]") {
    SECTION("void") {
        asyncio::Promise<void, std::error_code> promise1;
        asyncio::Promise<void, std::error_code> promise2;

        auto task = all(std::array{
            from(asyncio::task::CancellableFuture{
                promise1.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise1.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise2.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise2.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            })
        });

        SECTION("success") {
            promise1.resolve();
            promise2.resolve();
            REQUIRE(co_await task);
        }

        SECTION("failure") {
            promise1.resolve();
            promise2.reject(make_error_code(std::errc::invalid_argument));
            REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
        }
    }

    SECTION("not void") {
        asyncio::Promise<int, std::error_code> promise1;
        asyncio::Promise<int, std::error_code> promise2;

        auto task = all(std::array{
            from(asyncio::task::CancellableFuture{
                promise1.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise1.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise2.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise2.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            })
        });

        SECTION("success") {
            promise1.resolve(10);
            promise2.resolve(100);

            const auto result = co_await task;
            REQUIRE(result);
            REQUIRE(result->at(0) == 10);
            REQUIRE(result->at(1) == 100);
        }

        SECTION("failure") {
            promise1.resolve(10);
            promise2.reject(make_error_code(std::errc::invalid_argument));
            REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
        }
    }
}

ASYNC_TEST_CASE("task variadic all - error", "[task]") {
    SECTION("same types") {
        SECTION("void") {
            asyncio::Promise<void, std::error_code> promise1;
            asyncio::Promise<void, std::error_code> promise2;

            auto task = all(
                from(asyncio::task::CancellableFuture{
                    promise1.getFuture(),
                    [&]() -> std::expected<void, std::error_code> {
                        if (promise1.isFulfilled())
                            return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                        promise1.reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                }),
                from(asyncio::task::CancellableFuture{
                    promise2.getFuture(),
                    [&]() -> std::expected<void, std::error_code> {
                        if (promise2.isFulfilled())
                            return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                        promise2.reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                })
            );

            SECTION("success") {
                promise1.resolve();
                promise2.resolve();
                REQUIRE(co_await task);
            }

            SECTION("failure") {
                promise1.resolve();
                promise2.reject(make_error_code(std::errc::invalid_argument));
                REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
            }

            SECTION("cancel") {
                REQUIRE(task.cancel());
                REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
            }
        }

        SECTION("not void") {
            asyncio::Promise<int, std::error_code> promise1;
            asyncio::Promise<int, std::error_code> promise2;

            auto task = all(
                from(asyncio::task::CancellableFuture{
                    promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise1.isFulfilled())
                            return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                        promise1.reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                }),
                from(asyncio::task::CancellableFuture{
                    promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise2.isFulfilled())
                            return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                        promise2.reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                })
            );

            SECTION("success") {
                promise1.resolve(10);
                promise2.resolve(100);

                const auto result = co_await task;
                REQUIRE(result);
                REQUIRE(result->at(0) == 10);
                REQUIRE(result->at(1) == 100);
            }

            SECTION("failure") {
                promise1.resolve();
                promise2.reject(make_error_code(std::errc::invalid_argument));
                REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
            }

            SECTION("cancel") {
                REQUIRE(task.cancel());
                REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
            }
        }
    }

    SECTION("different types") {
        asyncio::Promise<int, std::error_code> promise1;
        asyncio::Promise<void, std::error_code> promise2;
        asyncio::Promise<long, std::error_code> promise3;

        auto task = all(
            from(asyncio::task::CancellableFuture{
                promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise1.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise2.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise3.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise3.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise3.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            })
        );

        SECTION("success") {
            promise1.resolve(10);
            promise2.resolve();
            promise3.resolve(100);

            const auto result = co_await task;
            REQUIRE(result);
            REQUIRE(std::get<0>(*result) == 10);
            REQUIRE(std::get<2>(*result) == 100);
        }

        SECTION("failure") {
            promise1.resolve(100);
            promise2.resolve();
            promise3.reject(make_error_code(std::errc::invalid_argument));
            REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
        }
    }
}

ASYNC_TEST_CASE("task allSettled - error", "[task]") {
    SECTION("void") {
        asyncio::Promise<void, std::error_code> promise1;
        asyncio::Promise<void, std::error_code> promise2;

        auto task = allSettled(std::array{
            from(asyncio::task::CancellableFuture{
                promise1.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise1.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise2.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise2.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            })
        });

        SECTION("normal") {
            promise1.resolve();
            promise2.reject(make_error_code(std::errc::invalid_argument));

            const auto result = co_await task;
            REQUIRE(result[0]);
            REQUIRE_FALSE(result[1]);
            REQUIRE(result[1].error() == std::errc::invalid_argument);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());

            const auto result = co_await task;
            REQUIRE_ERROR(result[0], std::errc::operation_canceled);
            REQUIRE_ERROR(result[1], std::errc::operation_canceled);
        }
    }

    SECTION("not void") {
        asyncio::Promise<int, std::error_code> promise1;
        asyncio::Promise<int, std::error_code> promise2;

        auto task = allSettled(std::array{
            from(asyncio::task::CancellableFuture{
                promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise1.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise2.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            })
        });

        SECTION("normal") {
            promise1.resolve(100);
            promise2.reject(make_error_code(std::errc::invalid_argument));

            const auto result = co_await task;
            REQUIRE(result[0] == 100);
            REQUIRE_ERROR(result[1], std::errc::invalid_argument);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());

            const auto result = co_await task;
            REQUIRE_ERROR(result[0], std::errc::operation_canceled);
            REQUIRE_ERROR(result[1], std::errc::operation_canceled);
        }
    }
}

ASYNC_TEST_CASE("task variadic allSettled - error", "[task]") {
    asyncio::Promise<int, std::error_code> promise1;
    asyncio::Promise<void, std::error_code> promise2;
    asyncio::Promise<long, std::error_code> promise3;

    auto task = allSettled(
        from(asyncio::task::CancellableFuture{
            promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                if (promise1.isFulfilled())
                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                promise1.reject(asyncio::task::Error::CANCELLED);
                return {};
            }
        }),
        from(asyncio::task::CancellableFuture{
            promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                if (promise2.isFulfilled())
                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                promise2.reject(asyncio::task::Error::CANCELLED);
                return {};
            }
        }),
        from(asyncio::task::CancellableFuture{
            promise3.getFuture(), [&]() -> std::expected<void, std::error_code> {
                if (promise3.isFulfilled())
                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                promise3.reject(asyncio::task::Error::CANCELLED);
                return {};
            }
        })
    );

    SECTION("normal") {
        promise1.resolve(10);
        promise2.resolve();
        promise3.reject(make_error_code(std::errc::invalid_argument));

        const auto result = co_await task;
        REQUIRE(std::get<0>(result) == 10);
        REQUIRE(std::get<1>(result));
        REQUIRE_ERROR(std::get<2>(result), std::errc::invalid_argument);
    }

    SECTION("cancel") {
        REQUIRE(task.cancel());

        const auto result = co_await task;
        REQUIRE_ERROR(std::get<0>(result), std::errc::operation_canceled);
        REQUIRE_ERROR(std::get<1>(result), std::errc::operation_canceled);
        REQUIRE_ERROR(std::get<2>(result), std::errc::operation_canceled);
    }
}

ASYNC_TEST_CASE("task any - error", "[task]") {
    SECTION("void") {
        asyncio::Promise<void, std::error_code> promise1;
        asyncio::Promise<void, std::error_code> promise2;

        auto task = any(std::array{
            from(asyncio::task::CancellableFuture{
                promise1.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise1.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise2.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise2.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            })
        });

        SECTION("success") {
            promise1.reject(make_error_code(std::errc::invalid_argument));
            promise2.resolve();
            REQUIRE(co_await task);
        }

        SECTION("failure") {
            promise1.reject(make_error_code(std::errc::invalid_argument));
            promise2.reject(make_error_code(std::errc::io_error));

            const auto result = co_await task;
            REQUIRE_FALSE(result);
            REQUIRE(result.error()[0] == std::errc::invalid_argument);
            REQUIRE(result.error()[1] == std::errc::io_error);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());

            const auto result = co_await task;
            REQUIRE_FALSE(result);
            REQUIRE(result.error()[0] == std::errc::operation_canceled);
            REQUIRE(result.error()[1] == std::errc::operation_canceled);
        }
    }

    SECTION("not void") {
        asyncio::Promise<int, std::error_code> promise1;
        asyncio::Promise<int, std::error_code> promise2;

        auto task = any(std::array{
            from(asyncio::task::CancellableFuture{
                promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise1.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise2.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            })
        });

        SECTION("success") {
            promise1.reject(make_error_code(std::errc::invalid_argument));
            promise2.resolve(100);
            REQUIRE(co_await task == 100);
        }

        SECTION("failure") {
            promise1.reject(make_error_code(std::errc::invalid_argument));
            promise2.reject(make_error_code(std::errc::io_error));

            const auto result = co_await task;
            REQUIRE_FALSE(result);
            REQUIRE(result.error()[0] == std::errc::invalid_argument);
            REQUIRE(result.error()[1] == std::errc::io_error);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());

            const auto result = co_await task;
            REQUIRE_FALSE(result);
            REQUIRE(result.error()[0] == std::errc::operation_canceled);
            REQUIRE(result.error()[1] == std::errc::operation_canceled);
        }
    }
}

ASYNC_TEST_CASE("task variadic any - error", "[task]") {
    SECTION("same types") {
        SECTION("void") {
            asyncio::Promise<void, std::error_code> promise1;
            asyncio::Promise<void, std::error_code> promise2;

            auto task = any(
                from(asyncio::task::CancellableFuture{
                    promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise1.isFulfilled())
                            return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                        promise1.reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                }),
                from(asyncio::task::CancellableFuture{
                    promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise2.isFulfilled())
                            return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                        promise2.reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                })
            );

            SECTION("success") {
                promise1.reject(make_error_code(std::errc::invalid_argument));
                promise2.resolve();
                REQUIRE(co_await task);
            }

            SECTION("failure") {
                promise1.reject(make_error_code(std::errc::invalid_argument));
                promise2.reject(make_error_code(std::errc::io_error));

                const auto result = co_await task;
                REQUIRE_FALSE(result);
                REQUIRE(result.error()[0] == std::errc::invalid_argument);
                REQUIRE(result.error()[1] == std::errc::io_error);
            }

            SECTION("cancel") {
                REQUIRE(task.cancel());

                const auto result = co_await task;
                REQUIRE_FALSE(result);
                REQUIRE(result.error()[0] == std::errc::operation_canceled);
                REQUIRE(result.error()[1] == std::errc::operation_canceled);
            }
        }

        SECTION("not void") {
            asyncio::Promise<int, std::error_code> promise1;
            asyncio::Promise<int, std::error_code> promise2;

            auto task = any(
                from(asyncio::task::CancellableFuture{
                    promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise1.isFulfilled())
                            return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                        promise1.reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                }),
                from(asyncio::task::CancellableFuture{
                    promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise2.isFulfilled())
                            return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                        promise2.reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                })
            );

            SECTION("success") {
                promise1.reject(make_error_code(std::errc::invalid_argument));
                promise2.resolve(100);
                REQUIRE(co_await task == 100);
            }

            SECTION("failure") {
                promise1.reject(make_error_code(std::errc::invalid_argument));
                promise2.reject(make_error_code(std::errc::io_error));

                const auto result = co_await task;
                REQUIRE_FALSE(result);
                REQUIRE(result.error()[0] == std::errc::invalid_argument);
                REQUIRE(result.error()[1] == std::errc::io_error);
            }

            SECTION("cancel") {
                REQUIRE(task.cancel());

                const auto result = co_await task;
                REQUIRE_FALSE(result);
                REQUIRE(result.error()[0] == std::errc::operation_canceled);
                REQUIRE(result.error()[1] == std::errc::operation_canceled);
            }
        }
    }

#if !defined(_LIBCPP_VERSION) || _LIBCPP_VERSION >= 190000
    SECTION("different types") {
        asyncio::Promise<int, std::error_code> promise1;
        asyncio::Promise<void, std::error_code> promise2;
        asyncio::Promise<long, std::error_code> promise3;

        auto task = any(
            from(asyncio::task::CancellableFuture{
                promise1.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise1.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise2.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise2.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise3.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    if (promise3.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise3.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            })
        );

        SECTION("success") {
            SECTION("no value") {
                promise1.reject(make_error_code(std::errc::invalid_argument));
                promise2.resolve();

                const auto result = co_await task;
                REQUIRE(result);
                REQUIRE_FALSE(result->has_value());
            }

            SECTION("has value") {
                promise1.reject(make_error_code(std::errc::invalid_argument));
                promise2.reject(make_error_code(std::errc::io_error));
                promise3.resolve(1000);

                const auto result = co_await task;
                REQUIRE(result);
                REQUIRE(result->has_value());

#if defined(_CPPRTTI) || defined(__GXX_RTTI)
                REQUIRE(result->type() == typeid(long));
#endif
                REQUIRE(std::any_cast<long>(*result) == 1000);
            }
        }

        SECTION("failure") {
            promise1.reject(make_error_code(std::errc::io_error));
            promise2.reject(make_error_code(std::errc::invalid_argument));
            promise3.reject(make_error_code(std::errc::bad_message));

            const auto result = co_await task;
            REQUIRE_FALSE(result);
            REQUIRE(result.error()[0] == std::errc::io_error);
            REQUIRE(result.error()[1] == std::errc::invalid_argument);
            REQUIRE(result.error()[2] == std::errc::bad_message);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());

            const auto result = co_await task;
            REQUIRE_FALSE(result);
            REQUIRE(result.error()[0] == std::errc::operation_canceled);
            REQUIRE(result.error()[1] == std::errc::operation_canceled);
            REQUIRE(result.error()[2] == std::errc::operation_canceled);
        }
    }
#endif
}

ASYNC_TEST_CASE("task race - error", "[task]") {
    SECTION("void") {
        asyncio::Promise<void, std::error_code> promise1;
        asyncio::Promise<void, std::error_code> promise2;

        auto task = race(std::array{
            from(asyncio::task::CancellableFuture{
                promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise1.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise2.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            })
        });

        SECTION("success") {
            promise1.resolve();
            REQUIRE(co_await task);
        }

        SECTION("failure") {
            promise1.reject(make_error_code(std::errc::invalid_argument));
            REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
        }
    }

    SECTION("not void") {
        asyncio::Promise<int, std::error_code> promise1;
        asyncio::Promise<int, std::error_code> promise2;

        auto task = race(std::array{
            from(asyncio::task::CancellableFuture{
                promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise1.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise2.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            })
        });

        SECTION("success") {
            promise1.resolve(10);
            REQUIRE(co_await task == 10);
        }

        SECTION("failure") {
            promise1.reject(make_error_code(std::errc::invalid_argument));
            REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
        }
    }
}

ASYNC_TEST_CASE("task variadic race - error", "[task]") {
    SECTION("same types") {
        SECTION("void") {
            asyncio::Promise<void, std::error_code> promise1;
            asyncio::Promise<void, std::error_code> promise2;

            auto task = race(
                from(asyncio::task::CancellableFuture{
                    promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise1.isFulfilled())
                            return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                        promise1.reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                }),
                from(asyncio::task::CancellableFuture{
                    promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise2.isFulfilled())
                            return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                        promise2.reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                })
            );

            SECTION("success") {
                promise1.resolve();
                REQUIRE(co_await task);
            }

            SECTION("failure") {
                promise1.reject(make_error_code(std::errc::invalid_argument));
                REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
            }

            SECTION("cancel") {
                REQUIRE(task.cancel());
                REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
            }
        }

        SECTION("not void") {
            asyncio::Promise<int, std::error_code> promise1;
            asyncio::Promise<int, std::error_code> promise2;

            auto task = race(
                from(asyncio::task::CancellableFuture{
                    promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise1.isFulfilled())
                            return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                        promise1.reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                }),
                from(asyncio::task::CancellableFuture{
                    promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                        if (promise2.isFulfilled())
                            return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                        promise2.reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                })
            );

            SECTION("success") {
                promise1.resolve(10);
                REQUIRE(co_await task == 10);
            }

            SECTION("failure") {
                promise1.reject(make_error_code(std::errc::invalid_argument));
                REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
            }

            SECTION("cancel") {
                REQUIRE(task.cancel());
                REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
            }
        }
    }

#if !defined(_LIBCPP_VERSION) || _LIBCPP_VERSION >= 190000
    SECTION("different types") {
        asyncio::Promise<int, std::error_code> promise1;
        asyncio::Promise<void, std::error_code> promise2;
        asyncio::Promise<long, std::error_code> promise3;

        auto task = race(
            from(asyncio::task::CancellableFuture{
                promise1.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise1.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise1.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise2.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise2.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise2.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            }),
            from(asyncio::task::CancellableFuture{
                promise3.getFuture(), [&]() -> std::expected<void, std::error_code> {
                    if (promise3.isFulfilled())
                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                    promise3.reject(asyncio::task::Error::CANCELLED);
                    return {};
                }
            })
        );

        SECTION("success") {
            SECTION("no value") {
                promise2.resolve();

                const auto result = co_await task;
                REQUIRE(result);
                REQUIRE_FALSE(result->has_value());
            }

            SECTION("has value") {
                promise1.resolve(10);

                const auto result = co_await task;
                REQUIRE(result);
                REQUIRE(result->has_value());

#if defined(_CPPRTTI) || defined(__GXX_RTTI)
                REQUIRE(result->type() == typeid(int));
#endif
                REQUIRE(std::any_cast<int>(*result) == 10);
            }
        }

        SECTION("failure") {
            promise1.reject(make_error_code(std::errc::invalid_argument));
            REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
        }

        SECTION("cancel") {
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
        }
    }
#endif
}

ASYNC_TEST_CASE("task transform - error", "[task]") {
    SECTION("sync") {
        asyncio::Promise<int, std::error_code> promise;
        auto task = asyncio::task::from(promise.getFuture())
            .transform([](const auto &value) {
                return value * 10;
            });

        SECTION("success") {
            promise.resolve(10);
            REQUIRE(co_await task == 100);
        }

        SECTION("failure") {
            promise.reject(make_error_code(std::errc::invalid_argument));
            REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
        }
    }

    SECTION("async") {
        asyncio::Promise<int, std::error_code> promise;
        auto task = asyncio::task::from(promise.getFuture())
            .transform([](const auto &value) -> asyncio::task::Task<int> {
                co_await asyncio::reschedule();
                co_return value * 10;
            });

        SECTION("success") {
            promise.resolve(10);
            REQUIRE(co_await task == 100);
        }

        SECTION("failure") {
            promise.reject(make_error_code(std::errc::invalid_argument));
            REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
        }
    }
}

ASYNC_TEST_CASE("task transform error - error", "[task]") {
    SECTION("sync") {
        asyncio::Promise<int, std::error_code> promise;
        auto task = asyncio::task::from(promise.getFuture())
            .transformError([](const auto &ec) {
                return ec.value();
            });

        SECTION("success") {
            promise.resolve(10);
            REQUIRE(co_await task == 10);
        }

        SECTION("failure") {
            promise.reject(make_error_code(std::errc::invalid_argument));
            REQUIRE_ERROR(co_await task, std::to_underlying(std::errc::invalid_argument));
        }
    }

    SECTION("async") {
        asyncio::Promise<int, std::error_code> promise;
        auto task = asyncio::task::from(promise.getFuture())
            .transformError([](const auto &ec) -> asyncio::task::Task<int> {
                co_await asyncio::reschedule();
                co_return ec.value();
            });

        SECTION("success") {
            promise.resolve(10);
            REQUIRE(co_await task == 10);
        }

        SECTION("failure") {
            promise.reject(make_error_code(std::errc::invalid_argument));
            REQUIRE_ERROR(co_await task, std::to_underlying(std::errc::invalid_argument));
        }
    }
}

ASYNC_TEST_CASE("task and then - error", "[task]") {
    SECTION("sync") {
        asyncio::Promise<int, std::error_code> promise;
        auto task = asyncio::task::from(promise.getFuture())
            .andThen([](const auto &value) -> std::expected<int, std::error_code> {
                if (value % 2)
                    return std::unexpected{make_error_code(std::errc::invalid_argument)};

                return value * 10;
            });

        SECTION("success") {
            promise.resolve(10);
            REQUIRE(co_await task == 100);
        }

        SECTION("failure") {
            SECTION("external error") {
                promise.reject(make_error_code(std::errc::invalid_argument));
                REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
            }

            SECTION("internal error") {
                promise.resolve(11);
                REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
            }
        }
    }

    SECTION("async") {
        asyncio::Promise<int, std::error_code> promise;
        auto task = asyncio::task::from(promise.getFuture())
            .andThen([](const auto &value) -> asyncio::task::Task<int, std::error_code> {
                co_await asyncio::reschedule();

                if (value % 2)
                    co_return std::unexpected{make_error_code(std::errc::invalid_argument)};

                co_return value * 10;
            });

        SECTION("success") {
            promise.resolve(10);
            REQUIRE(co_await task == 100);
        }

        SECTION("failure") {
            SECTION("external error") {
                promise.reject(make_error_code(std::errc::invalid_argument));
                REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
            }

            SECTION("internal error") {
                promise.resolve(11);
                REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
            }
        }
    }
}

ASYNC_TEST_CASE("task or else - error", "[task]") {
    SECTION("sync") {
        asyncio::Promise<int, std::error_code> promise;
        auto task = asyncio::task::from(promise.getFuture())
            .orElse([](const auto &ec) -> std::expected<int, std::error_code> {
                if (ec != std::errc::io_error)
                    return std::unexpected{ec};

                return ec.value();
            });

        SECTION("success") {
            SECTION("external") {
                promise.resolve(10);
                REQUIRE(co_await task == 10);
            }

            SECTION("internal") {
                promise.reject(make_error_code(std::errc::io_error));
                REQUIRE(co_await task == std::to_underlying(std::errc::io_error));
            }
        }

        SECTION("failure") {
            promise.reject(make_error_code(std::errc::invalid_argument));
            REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
        }
    }

    SECTION("async") {
        asyncio::Promise<int, std::error_code> promise;
        auto task = asyncio::task::from(promise.getFuture())
            .orElse([](const auto &ec) -> asyncio::task::Task<int, std::error_code> {
                co_await asyncio::reschedule();

                if (ec != std::errc::io_error)
                    co_return std::unexpected{ec};

                co_return ec.value();
            });

        SECTION("success") {
            SECTION("external") {
                promise.resolve(10);
                REQUIRE(co_await task == 10);
            }

            SECTION("internal") {
                promise.reject(make_error_code(std::errc::io_error));
                REQUIRE(co_await task == std::to_underlying(std::errc::io_error));
            }
        }

        SECTION("failure") {
            promise.reject(make_error_code(std::errc::invalid_argument));
            REQUIRE_ERROR(co_await task, std::errc::invalid_argument);
        }
    }
}

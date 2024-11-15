#include <asyncio/task.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

TEST_CASE("task with error", "[task]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        SECTION("success") {
            asyncio::Promise<int, std::error_code> promise;
            auto task = asyncio::task::from(promise.getFuture());
            promise.resolve(10);

            const auto &res = co_await task;
            REQUIRE(res);
            REQUIRE(*res == 10);
        }

        SECTION("failure") {
            asyncio::Promise<int, std::error_code> promise;

            auto task = asyncio::task::from(promise.getFuture());
            promise.reject(make_error_code(std::errc::invalid_argument));

            const auto &res = co_await task;
            REQUIRE_FALSE(res);
            REQUIRE(res.error() == std::errc::invalid_argument);
        }

        SECTION("cancel") {
            asyncio::Promise<int, std::error_code> promise;
            auto task = from(asyncio::task::Cancellable{
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

            const auto &res = co_await task;
            REQUIRE_FALSE(res);
            REQUIRE(res.error() == std::errc::operation_canceled);
        }

        SECTION("check cancelled") {
            const auto promise = std::make_shared<asyncio::Promise<int, std::error_code>>();

            auto task = [](auto p) -> asyncio::task::Task<void> {
                bool cancelled = co_await asyncio::task::cancelled;
                REQUIRE_FALSE(cancelled);

                const auto res = co_await asyncio::task::Cancellable{
                    p->getFuture(),
                    [&]() -> std::expected<void, std::error_code> {
                        p->reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                };
                REQUIRE_FALSE(res);
                REQUIRE(res.error() == std::errc::operation_canceled);

                cancelled = co_await asyncio::task::cancelled;
                REQUIRE(cancelled);
            }(promise);
            REQUIRE_FALSE(task.cancelled());
            REQUIRE(task.cancel());
            REQUIRE(task.cancelled());

            co_await task;
        }

        SECTION("lock") {
            const auto promise1 = std::make_shared<asyncio::Promise<int, std::error_code>>();
            const auto promise2 = std::make_shared<asyncio::Promise<int, std::error_code>>();

            auto task = [](auto p1, auto p2) -> asyncio::task::Task<void> {
                bool cancelled = co_await asyncio::task::cancelled;
                REQUIRE_FALSE(cancelled);

                co_await asyncio::task::lock;

                auto res = co_await asyncio::task::Cancellable{
                    p1->getFuture(),
                    [&]() -> std::expected<void, std::error_code> {
                        p1->reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                };
                REQUIRE(res);
                REQUIRE(*res == 10);

                co_await asyncio::task::unlock;

                cancelled = co_await asyncio::task::cancelled;
                REQUIRE(cancelled);

                res = co_await asyncio::task::Cancellable{
                    p2->getFuture(),
                    [&]() -> std::expected<void, std::error_code> {
                        p2->reject(asyncio::task::Error::CANCELLED);
                        return {};
                    }
                };
                REQUIRE_FALSE(res);
                REQUIRE(res.error() == std::errc::operation_canceled);

                cancelled = co_await asyncio::task::cancelled;
                REQUIRE(cancelled);
            }(promise1, promise2);
            REQUIRE(task.locked());
            REQUIRE_FALSE(task.cancelled());

            const auto res = task.cancel();
            REQUIRE_FALSE(res);
            REQUIRE(res.error() == asyncio::task::Error::LOCKED);
            REQUIRE(task.cancelled());

            promise1->resolve(10);
            co_await task;
        }

        SECTION("trace") {
            asyncio::Promise<int, std::error_code> promise;
            auto task = asyncio::task::from(promise.getFuture());
            REQUIRE_THAT(task.trace(), Catch::Matchers::ContainsSubstring("from"));

            promise.resolve(10);

            const auto &res = co_await task;
            REQUIRE_THAT(task.trace(), Catch::Matchers::IsEmpty());
            REQUIRE(res);
            REQUIRE(*res == 10);
        }

        SECTION("ranges") {
            SECTION("void") {
                asyncio::Promise<void, std::error_code> promise1;
                asyncio::Promise<void, std::error_code> promise2;

                SECTION("all") {
                    auto task = all(std::array{
                        from(asyncio::task::Cancellable{
                            promise1.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise1.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise1.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
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

                        const auto res = co_await task;
                        REQUIRE(res);
                    }

                    SECTION("failure") {
                        promise1.resolve();
                        promise2.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::invalid_argument);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::operation_canceled);
                    }
                }

                SECTION("allSettled") {
                    auto task = allSettled(std::array{
                        from(asyncio::task::Cancellable{
                            promise1.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise1.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise1.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
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

                        const auto res = co_await task;
                        REQUIRE(res[0]);
                        REQUIRE(res[1]);
                    }

                    SECTION("failure") {
                        promise1.resolve();
                        promise2.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE(res[0]);
                        REQUIRE_FALSE(res[1]);
                        REQUIRE(res[1].error() == std::errc::invalid_argument);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res[0]);
                        REQUIRE(res[0].error() == std::errc::operation_canceled);
                        REQUIRE_FALSE(res[1]);
                        REQUIRE(res[1].error() == std::errc::operation_canceled);
                    }
                }

                SECTION("any") {
                    auto task = any(std::array{
                        from(asyncio::task::Cancellable{
                            promise1.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise1.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise1.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
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

                        const auto res = co_await task;
                        REQUIRE(res);
                    }

                    SECTION("failure") {
                        promise1.reject(make_error_code(std::errc::invalid_argument));
                        promise2.reject(make_error_code(std::errc::io_error));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error()[0] == std::errc::invalid_argument);
                        REQUIRE(res.error()[1] == std::errc::io_error);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error()[0] == std::errc::operation_canceled);
                        REQUIRE(res.error()[1] == std::errc::operation_canceled);
                    }
                }

                SECTION("race") {
                    auto task = race(std::array{
                        from(asyncio::task::Cancellable{
                            promise1.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise1.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise1.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
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
                        const auto res = co_await task;
                        REQUIRE(res);
                    }

                    SECTION("failure") {
                        promise1.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::invalid_argument);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::operation_canceled);
                    }
                }
            }

            SECTION("not void") {
                asyncio::Promise<int, std::error_code> promise1;
                asyncio::Promise<int, std::error_code> promise2;

                SECTION("all") {
                    auto task = all(std::array{
                        from(asyncio::task::Cancellable{
                            promise1.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise1.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise1.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
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

                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(res->at(0) == 10);
                        REQUIRE(res->at(1) == 100);
                    }

                    SECTION("failure") {
                        promise1.resolve(10);
                        promise2.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::invalid_argument);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::operation_canceled);
                    }
                }

                SECTION("allSettled") {
                    auto task = allSettled(std::array{
                        from(asyncio::task::Cancellable{
                            promise1.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise1.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise1.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
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

                        const auto res = co_await task;
                        REQUIRE(res[0]);
                        REQUIRE(*res[0] == 10);
                        REQUIRE(res[1]);
                        REQUIRE(*res[1] == 100);
                    }

                    SECTION("failure") {
                        promise1.resolve(10);
                        promise2.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE(res[0]);
                        REQUIRE(*res[0] == 10);
                        REQUIRE_FALSE(res[1]);
                        REQUIRE(res[1].error() == std::errc::invalid_argument);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res[0]);
                        REQUIRE(res[0].error() == std::errc::operation_canceled);
                        REQUIRE_FALSE(res[1]);
                        REQUIRE(res[1].error() == std::errc::operation_canceled);
                    }
                }

                SECTION("any") {
                    auto task = any(std::array{
                        from(asyncio::task::Cancellable{
                            promise1.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise1.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise1.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
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
                        promise2.resolve(100);

                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(*res == 100);
                    }

                    SECTION("failure") {
                        promise1.reject(make_error_code(std::errc::invalid_argument));
                        promise2.reject(make_error_code(std::errc::io_error));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error()[0] == std::errc::invalid_argument);
                        REQUIRE(res.error()[1] == std::errc::io_error);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error()[0] == std::errc::operation_canceled);
                        REQUIRE(res.error()[1] == std::errc::operation_canceled);
                    }
                }

                SECTION("race") {
                    auto task = race(std::array{
                        from(asyncio::task::Cancellable{
                            promise1.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise1.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise1.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
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

                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(*res == 10);
                    }

                    SECTION("failure") {
                        promise1.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::invalid_argument);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::operation_canceled);
                    }
                }
            }
        }

        SECTION("variadic") {
            SECTION("same types") {
                SECTION("void") {
                    asyncio::Promise<void, std::error_code> promise1;
                    asyncio::Promise<void, std::error_code> promise2;

                    SECTION("all") {
                        auto task = all(
                            from(asyncio::task::Cancellable{
                                promise1.getFuture(),
                                [&]() -> std::expected<void, std::error_code> {
                                    if (promise1.isFulfilled())
                                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                    promise1.reject(asyncio::task::Error::CANCELLED);
                                    return {};
                                }
                            }),
                            from(asyncio::task::Cancellable{
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

                            const auto res = co_await task;
                            REQUIRE(res);
                        }

                        SECTION("failure") {
                            promise1.resolve();
                            promise2.reject(make_error_code(std::errc::invalid_argument));

                            const auto res = co_await task;
                            REQUIRE_FALSE(res);
                            REQUIRE(res.error() == std::errc::invalid_argument);
                        }

                        SECTION("cancel") {
                            REQUIRE(task.cancel());

                            const auto res = co_await task;
                            REQUIRE_FALSE(res);
                            REQUIRE(res.error() == std::errc::operation_canceled);
                        }
                    }

                    SECTION("allSettled") {
                        auto task = allSettled(
                            from(asyncio::task::Cancellable{
                                promise1.getFuture(),
                                [&]() -> std::expected<void, std::error_code> {
                                    if (promise1.isFulfilled())
                                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                    promise1.reject(asyncio::task::Error::CANCELLED);
                                    return {};
                                }
                            }),
                            from(asyncio::task::Cancellable{
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

                            const auto res = co_await task;
                            REQUIRE(std::get<0>(res));
                            REQUIRE(std::get<1>(res));
                        }

                        SECTION("failure") {
                            promise1.resolve();
                            promise2.reject(make_error_code(std::errc::invalid_argument));

                            const auto res = co_await task;
                            REQUIRE(std::get<0>(res));
                            REQUIRE_FALSE(std::get<1>(res));
                            REQUIRE(std::get<1>(res).error() == std::errc::invalid_argument);
                        }

                        SECTION("cancel") {
                            REQUIRE(task.cancel());

                            const auto res = co_await task;
                            REQUIRE_FALSE(std::get<0>(res));
                            REQUIRE(std::get<0>(res).error() == std::errc::operation_canceled);
                            REQUIRE_FALSE(std::get<1>(res));
                            REQUIRE(std::get<1>(res).error() == std::errc::operation_canceled);
                        }
                    }

                    SECTION("any") {
                        auto task = any(
                            from(asyncio::task::Cancellable{
                                promise1.getFuture(),
                                [&]() -> std::expected<void, std::error_code> {
                                    if (promise1.isFulfilled())
                                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                    promise1.reject(asyncio::task::Error::CANCELLED);
                                    return {};
                                }
                            }),
                            from(asyncio::task::Cancellable{
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
                            promise1.reject(make_error_code(std::errc::invalid_argument));
                            promise2.resolve();

                            const auto res = co_await task;
                            REQUIRE(res);
                        }

                        SECTION("failure") {
                            promise1.reject(make_error_code(std::errc::invalid_argument));
                            promise2.reject(make_error_code(std::errc::io_error));

                            const auto res = co_await task;
                            REQUIRE_FALSE(res);
                            REQUIRE(res.error()[0] == std::errc::invalid_argument);
                            REQUIRE(res.error()[1] == std::errc::io_error);
                        }

                        SECTION("cancel") {
                            REQUIRE(task.cancel());

                            const auto res = co_await task;
                            REQUIRE_FALSE(res);
                            REQUIRE(res.error()[0] == std::errc::operation_canceled);
                            REQUIRE(res.error()[1] == std::errc::operation_canceled);
                        }
                    }

                    SECTION("race") {
                        auto task = race(
                            from(asyncio::task::Cancellable{
                                promise1.getFuture(),
                                [&]() -> std::expected<void, std::error_code> {
                                    if (promise1.isFulfilled())
                                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                    promise1.reject(asyncio::task::Error::CANCELLED);
                                    return {};
                                }
                            }),
                            from(asyncio::task::Cancellable{
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
                            const auto res = co_await task;
                            REQUIRE(res);
                        }

                        SECTION("failure") {
                            promise1.reject(make_error_code(std::errc::invalid_argument));

                            const auto res = co_await task;
                            REQUIRE_FALSE(res);
                            REQUIRE(res.error() == std::errc::invalid_argument);
                        }

                        SECTION("cancel") {
                            REQUIRE(task.cancel());

                            const auto res = co_await task;
                            REQUIRE_FALSE(res);
                            REQUIRE(res.error() == std::errc::operation_canceled);
                        }
                    }
                }

                SECTION("not void") {
                    asyncio::Promise<int, std::error_code> promise1;
                    asyncio::Promise<int, std::error_code> promise2;

                    SECTION("all") {
                        auto task = all(
                            from(asyncio::task::Cancellable{
                                promise1.getFuture(),
                                [&]() -> std::expected<void, std::error_code> {
                                    if (promise1.isFulfilled())
                                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                    promise1.reject(asyncio::task::Error::CANCELLED);
                                    return {};
                                }
                            }),
                            from(asyncio::task::Cancellable{
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
                            promise1.resolve(10);
                            promise2.resolve(100);

                            const auto res = co_await task;
                            REQUIRE(res);
                            REQUIRE(res->at(0) == 10);
                            REQUIRE(res->at(1) == 100);
                        }

                        SECTION("failure") {
                            promise1.resolve(10);
                            promise2.reject(make_error_code(std::errc::invalid_argument));

                            const auto res = co_await task;
                            REQUIRE_FALSE(res);
                            REQUIRE(res.error() == std::errc::invalid_argument);
                        }

                        SECTION("cancel") {
                            REQUIRE(task.cancel());

                            const auto res = co_await task;
                            REQUIRE_FALSE(res);
                            REQUIRE(res.error() == std::errc::operation_canceled);
                        }
                    }

                    SECTION("allSettled") {
                        auto task = allSettled(
                            from(asyncio::task::Cancellable{
                                promise1.getFuture(),
                                [&]() -> std::expected<void, std::error_code> {
                                    if (promise1.isFulfilled())
                                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                    promise1.reject(asyncio::task::Error::CANCELLED);
                                    return {};
                                }
                            }),
                            from(asyncio::task::Cancellable{
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
                            promise1.resolve(10);
                            promise2.resolve(100);

                            const auto res = co_await task;
                            REQUIRE(res[0]);
                            REQUIRE(*res[0] == 10);
                            REQUIRE(res[1]);
                            REQUIRE(*res[1] == 100);
                        }

                        SECTION("failure") {
                            promise1.resolve(10);
                            promise2.reject(make_error_code(std::errc::invalid_argument));

                            const auto res = co_await task;
                            REQUIRE(res[0]);
                            REQUIRE(*res[0] == 10);
                            REQUIRE_FALSE(res[1]);
                            REQUIRE(res[1].error() == std::errc::invalid_argument);
                        }

                        SECTION("cancel") {
                            REQUIRE(task.cancel());

                            const auto res = co_await task;
                            REQUIRE_FALSE(res[0]);
                            REQUIRE(res[0].error() == std::errc::operation_canceled);
                            REQUIRE_FALSE(res[1]);
                            REQUIRE(res[1].error() == std::errc::operation_canceled);
                        }
                    }

                    SECTION("any") {
                        auto task = any(
                            from(asyncio::task::Cancellable{
                                promise1.getFuture(),
                                [&]() -> std::expected<void, std::error_code> {
                                    if (promise1.isFulfilled())
                                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                    promise1.reject(asyncio::task::Error::CANCELLED);
                                    return {};
                                }
                            }),
                            from(asyncio::task::Cancellable{
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
                            promise1.reject(make_error_code(std::errc::invalid_argument));
                            promise2.resolve(100);

                            const auto res = co_await task;
                            REQUIRE(res);
                            REQUIRE(*res == 100);
                        }

                        SECTION("failure") {
                            promise1.reject(make_error_code(std::errc::invalid_argument));
                            promise2.reject(make_error_code(std::errc::io_error));

                            const auto res = co_await task;
                            REQUIRE_FALSE(res);
                            REQUIRE(res.error()[0] == std::errc::invalid_argument);
                            REQUIRE(res.error()[1] == std::errc::io_error);
                        }

                        SECTION("cancel") {
                            REQUIRE(task.cancel());

                            const auto res = co_await task;
                            REQUIRE_FALSE(res);
                            REQUIRE(res.error()[0] == std::errc::operation_canceled);
                            REQUIRE(res.error()[1] == std::errc::operation_canceled);
                        }
                    }

                    SECTION("race") {
                        auto task = race(
                            from(asyncio::task::Cancellable{
                                promise1.getFuture(),
                                [&]() -> std::expected<void, std::error_code> {
                                    if (promise1.isFulfilled())
                                        return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                    promise1.reject(asyncio::task::Error::CANCELLED);
                                    return {};
                                }
                            }),
                            from(asyncio::task::Cancellable{
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
                            promise1.resolve(10);

                            const auto res = co_await task;
                            REQUIRE(res);
                            REQUIRE(*res == 10);
                        }

                        SECTION("failure") {
                            promise1.reject(make_error_code(std::errc::invalid_argument));

                            const auto res = co_await task;
                            REQUIRE_FALSE(res);
                            REQUIRE(res.error() == std::errc::invalid_argument);
                        }

                        SECTION("cancel") {
                            REQUIRE(task.cancel());

                            const auto res = co_await task;
                            REQUIRE_FALSE(res);
                            REQUIRE(res.error() == std::errc::operation_canceled);
                        }
                    }
                }
            }

            SECTION("different types") {
                asyncio::Promise<int, std::error_code> promise1;
                asyncio::Promise<void, std::error_code> promise2;
                asyncio::Promise<long, std::error_code> promise3;

                SECTION("all") {
                    auto task = all(
                        from(asyncio::task::Cancellable{
                            promise1.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise1.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise1.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
                            promise2.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise2.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise2.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
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
                        promise1.resolve(10);
                        promise2.resolve();
                        promise3.resolve(1000);

                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(std::get<0>(*res) == 10);
                        REQUIRE(std::get<2>(*res) == 1000);
                    }

                    SECTION("failure") {
                        promise1.resolve(100);
                        promise2.resolve();
                        promise3.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::invalid_argument);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::operation_canceled);
                    }
                }

                SECTION("allSettled") {
                    auto task = allSettled(
                        from(asyncio::task::Cancellable{
                            promise1.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise1.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise1.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
                            promise2.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise2.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise2.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
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
                        promise1.resolve(10);
                        promise2.resolve();
                        promise3.resolve(1000);

                        const auto res = co_await task;
                        REQUIRE(std::get<0>(res));
                        REQUIRE(*std::get<0>(res) == 10);
                        REQUIRE(std::get<1>(res));
                        REQUIRE(std::get<2>(res));
                        REQUIRE(*std::get<2>(res) == 1000);
                    }

                    SECTION("failure") {
                        promise1.resolve(10);
                        promise2.resolve();
                        promise3.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE(std::get<0>(res));
                        REQUIRE(*std::get<0>(res) == 10);
                        REQUIRE(std::get<1>(res));
                        REQUIRE_FALSE(std::get<2>(res));
                        REQUIRE(std::get<2>(res).error() == std::errc::invalid_argument);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(std::get<0>(res));
                        REQUIRE(std::get<0>(res).error() == std::errc::operation_canceled);
                        REQUIRE_FALSE(std::get<1>(res));
                        REQUIRE(std::get<1>(res).error() == std::errc::operation_canceled);
                        REQUIRE_FALSE(std::get<2>(res));
                        REQUIRE(std::get<2>(res).error() == std::errc::operation_canceled);
                    }
                }

#if !defined(_LIBCPP_VERSION) || _LIBCPP_VERSION >= 190000
                SECTION("any") {
                    auto task = any(
                        from(asyncio::task::Cancellable{
                            promise1.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise1.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise1.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
                            promise2.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise2.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise2.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
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

                            const auto res = co_await task;
                            REQUIRE(res);
                            REQUIRE_FALSE(res->has_value());
                        }

                        SECTION("has value") {
                            promise1.reject(make_error_code(std::errc::invalid_argument));
                            promise2.reject(make_error_code(std::errc::io_error));
                            promise3.resolve(1000);

                            const auto res = co_await task;
                            REQUIRE(res);
                            REQUIRE(res->has_value());

#if defined(_CPPRTTI) || defined(__GXX_RTTI)
                            REQUIRE(res->type() == typeid(long));
#endif
                            REQUIRE(std::any_cast<long>(*res) == 1000);
                        }
                    }

                    SECTION("failure") {
                        promise1.reject(make_error_code(std::errc::io_error));
                        promise2.reject(make_error_code(std::errc::invalid_argument));
                        promise3.reject(make_error_code(std::errc::bad_message));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error()[0] == std::errc::io_error);
                        REQUIRE(res.error()[1] == std::errc::invalid_argument);
                        REQUIRE(res.error()[2] == std::errc::bad_message);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error()[0] == std::errc::operation_canceled);
                        REQUIRE(res.error()[1] == std::errc::operation_canceled);
                        REQUIRE(res.error()[2] == std::errc::operation_canceled);
                    }
                }

                SECTION("race") {
                    auto task = race(
                        from(asyncio::task::Cancellable{
                            promise1.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise1.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise1.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
                            promise2.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise2.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise2.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }),
                        from(asyncio::task::Cancellable{
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
                            promise2.resolve();

                            const auto res = co_await task;
                            REQUIRE(res);
                            REQUIRE_FALSE(res->has_value());
                        }

                        SECTION("has value") {
                            promise1.resolve(10);

                            const auto res = co_await task;
                            REQUIRE(res);
                            REQUIRE(res->has_value());

#if defined(_CPPRTTI) || defined(__GXX_RTTI)
                            REQUIRE(res->type() == typeid(int));
#endif
                            REQUIRE(std::any_cast<int>(*res) == 10);
                        }
                    }

                    SECTION("failure") {
                        promise1.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::invalid_argument);
                    }

                    SECTION("cancel") {
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::operation_canceled);
                    }
                }
#endif
            }
        }

        SECTION("monadic operations") {
            SECTION("and then") {
                SECTION("normal") {
                    SECTION("success") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .andThen([](const int value) -> std::expected<int, std::error_code> {
                                return value * 10;
                            });

                        promise.resolve(10);

                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(*res == 100);
                    }

                    SECTION("failure") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .andThen([](const int value) -> std::expected<int, std::error_code> {
                                return value * 10;
                            });

                        promise.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::invalid_argument);
                    }

                    SECTION("cancel") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = from(asyncio::task::Cancellable{
                            promise.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }).andThen([](const int value) -> std::expected<int, std::error_code> {
                            return value * 10;
                        });
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::operation_canceled);
                    }
                }

                SECTION("coroutine") {
                    SECTION("success") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .andThen([](const int value) -> asyncio::task::Task<int, std::error_code> {
                                co_return value * 10;
                            });

                        promise.resolve(10);

                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(*res == 100);
                    }

                    SECTION("failure") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .andThen([](const int value) -> asyncio::task::Task<int, std::error_code> {
                                co_return value * 10;
                            });

                        promise.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::invalid_argument);
                    }

                    SECTION("cancel") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = from(asyncio::task::Cancellable{
                            promise.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }).andThen([](const int value) -> asyncio::task::Task<int, std::error_code> {
                            co_return value * 10;
                        });
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::operation_canceled);
                    }
                }
            }

            SECTION("transform") {
                SECTION("normal") {
                    SECTION("success") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .transform([](const int value) {
                                return value * 10;
                            });

                        promise.resolve(10);

                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(*res == 100);
                    }

                    SECTION("failure") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .transform([](const int value) {
                                return value * 10;
                            });

                        promise.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::invalid_argument);
                    }

                    SECTION("cancel") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = from(asyncio::task::Cancellable{
                            promise.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }).transform([](const int value) {
                            return value * 10;
                        });
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::operation_canceled);
                    }
                }

                SECTION("coroutine") {
                    SECTION("success") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .transform([](const int value) -> asyncio::task::Task<int> {
                                co_return value * 10;
                            });

                        promise.resolve(10);

                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(*res == 100);
                    }

                    SECTION("failure") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .transform([](const int value) -> asyncio::task::Task<int> {
                                co_return value * 10;
                            });

                        promise.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::invalid_argument);
                    }

                    SECTION("cancel") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = from(asyncio::task::Cancellable{
                            promise.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }).transform([](const int value) -> asyncio::task::Task<int> {
                            co_return value * 10;
                        });
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::operation_canceled);
                    }
                }
            }

            SECTION("or else") {
                SECTION("normal") {
                    SECTION("success") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .orElse([](const std::error_code &ec) -> std::expected<int, std::error_code> {
                                REQUIRE(ec == std::errc::invalid_argument);
                                return 1000;
                            });

                        promise.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(*res == 1000);
                    }

                    SECTION("failure") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .orElse([](const std::error_code &ec) -> std::expected<int, std::error_code> {
                                REQUIRE(ec == std::errc::invalid_argument);
                                return std::unexpected{make_error_code(std::errc::io_error)};
                            });

                        promise.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::io_error);
                    }

                    SECTION("cancel") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = from(asyncio::task::Cancellable{
                            promise.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }).orElse([](const std::error_code &ec) -> std::expected<int, std::error_code> {
                            REQUIRE(ec == std::errc::operation_canceled);
                            return std::unexpected{make_error_code(std::errc::invalid_argument)};
                        });
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::invalid_argument);
                    }
                }

                SECTION("coroutine") {
                    SECTION("success") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .orElse([](const std::error_code &ec) -> asyncio::task::Task<int, std::error_code> {
                                REQUIRE(ec == std::errc::invalid_argument);
                                co_return 1000;
                            });

                        promise.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(*res == 1000);
                    }

                    SECTION("failure") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .orElse([](const std::error_code &ec) -> asyncio::task::Task<int, std::error_code> {
                                REQUIRE(ec == std::errc::invalid_argument);
                                co_return std::unexpected{make_error_code(std::errc::io_error)};
                            });

                        promise.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::io_error);
                    }

                    SECTION("cancel") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = from(asyncio::task::Cancellable{
                            promise.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }).orElse([](const std::error_code &ec) -> asyncio::task::Task<int, std::error_code> {
                            REQUIRE(ec == std::errc::operation_canceled);
                            co_return std::unexpected{make_error_code(std::errc::invalid_argument)};
                        });
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::errc::invalid_argument);
                    }
                }
            }

            SECTION("transform error") {
                SECTION("normal") {
                    SECTION("success") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .transformError([](const std::error_code &ec) {
                                return ec.value();
                            });

                        promise.resolve(10);

                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(*res == 10);
                    }

                    SECTION("failure") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .transformError([](const std::error_code &ec) {
                                return ec.value();
                            });

                        promise.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::to_underlying(std::errc::invalid_argument));
                    }

                    SECTION("cancel") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = from(asyncio::task::Cancellable{
                            promise.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }).transformError([](const std::error_code &ec) {
                            return ec.value();
                        });
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::to_underlying(asyncio::task::Error::CANCELLED));
                    }
                }

                SECTION("coroutine") {
                    SECTION("success") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .transformError([](const std::error_code &ec) -> asyncio::task::Task<int> {
                                co_return ec.value();
                            });

                        promise.resolve(10);

                        const auto res = co_await task;
                        REQUIRE(res);
                        REQUIRE(*res == 10);
                    }

                    SECTION("failure") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = asyncio::task::from(promise.getFuture())
                            .transformError([](const std::error_code &ec) -> asyncio::task::Task<int> {
                                co_return ec.value();
                            });

                        promise.reject(make_error_code(std::errc::invalid_argument));

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::to_underlying(std::errc::invalid_argument));
                    }

                    SECTION("cancel") {
                        asyncio::Promise<int, std::error_code> promise;
                        auto task = from(asyncio::task::Cancellable{
                            promise.getFuture(),
                            [&]() -> std::expected<void, std::error_code> {
                                if (promise.isFulfilled())
                                    return std::unexpected{asyncio::task::Error::WILL_BE_DONE};

                                promise.reject(asyncio::task::Error::CANCELLED);
                                return {};
                            }
                        }).transformError([](const std::error_code &ec) -> asyncio::task::Task<int> {
                            co_return ec.value();
                        });
                        REQUIRE(task.cancel());

                        const auto res = co_await task;
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == std::to_underlying(asyncio::task::Error::CANCELLED));
                    }
                }
            }
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

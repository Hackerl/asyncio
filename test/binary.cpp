#include <asyncio/binary.h>
#include <asyncio/event_loop.h>
#include <asyncio/ev/pipe.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("binary transfer", "[binary]") {
    SECTION("little endian") {
        SECTION("int16_t") {
            SECTION("random") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeLE(buffer, std::int16_t{6789});
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readLE<std::int16_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == 6789);
                        }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("max") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeLE(
                                buffer,
                                (std::numeric_limits<std::int16_t>::max)()
                            );
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readLE<std::int16_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == (std::numeric_limits<std::int16_t>::max)());
                        }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("min") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeLE(
                                buffer,
                                (std::numeric_limits<std::int16_t>::min)()
                            );
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readLE<std::int16_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == (std::numeric_limits<std::int16_t>::min)());
                        }(std::move(buffers->at(1)))
                    );
                });
            }
        }

        SECTION("int32_t") {
            SECTION("random") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeLE(buffer, std::int32_t{6789});
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readLE<std::int32_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == 6789);
                        }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("max") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeLE(
                                buffer,
                                (std::numeric_limits<std::int32_t>::max)()
                            );
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readLE<std::int32_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == (std::numeric_limits<std::int32_t>::max)());
                        }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("min") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeLE(
                                buffer,
                                (std::numeric_limits<std::int32_t>::min)()
                            );
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readLE<std::int32_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == (std::numeric_limits<std::int32_t>::min)());
                        }(std::move(buffers->at(1)))
                    );
                });
            }
        }

        SECTION("int64_t") {
            SECTION("random") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeLE(buffer, std::int64_t{6789});
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readLE<std::int64_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == 6789);
                        }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("max") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeLE(
                                buffer,
                                (std::numeric_limits<std::int64_t>::max)()
                            );
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readLE<std::int64_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == (std::numeric_limits<std::int64_t>::max)());
                        }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("min") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeLE(
                                buffer,
                                (std::numeric_limits<std::int64_t>::min)()
                            );
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readLE<std::int64_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == (std::numeric_limits<std::int64_t>::min)());
                        }(std::move(buffers->at(1)))
                    );
                });
            }
        }
    }

    SECTION("big endian") {
        SECTION("int16_t") {
            SECTION("random") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeBE(buffer, std::int16_t{6789});
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readBE<std::int16_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == 6789);
                        }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("max") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeBE(
                                buffer,
                                (std::numeric_limits<std::int16_t>::max)()
                            );
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readBE<std::int16_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == (std::numeric_limits<std::int16_t>::max)());
                        }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("min") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeBE(
                                buffer,
                                (std::numeric_limits<std::int16_t>::min)()
                            );
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readBE<std::int16_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == (std::numeric_limits<std::int16_t>::min)());
                        }(std::move(buffers->at(1)))
                    );
                });
            }
        }

        SECTION("int32_t") {
            SECTION("random") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeBE(buffer, std::int32_t{6789});
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readBE<std::int32_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == 6789);
                        }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("max") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeBE(
                                buffer,
                                (std::numeric_limits<std::int32_t>::max)()
                            );
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readBE<std::int32_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == (std::numeric_limits<std::int32_t>::max)());
                        }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("min") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeBE(
                                buffer,
                                (std::numeric_limits<std::int32_t>::min)()
                            );
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readBE<std::int32_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == (std::numeric_limits<std::int32_t>::min)());
                        }(std::move(buffers->at(1)))
                    );
                });
            }
        }

        SECTION("int64_t") {
            SECTION("random") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeBE(buffer, std::int64_t{6789});
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readBE<std::int64_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == 6789);
                        }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("max") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeBE(
                                buffer,
                                (std::numeric_limits<std::int64_t>::max)()
                            );
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readBE<std::int64_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == (std::numeric_limits<std::int64_t>::max)());
                        }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("min") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::writeBE(
                                buffer,
                                (std::numeric_limits<std::int64_t>::min)()
                            );
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            const auto result = co_await asyncio::binary::readBE<std::int64_t>(buffer);
                            REQUIRE(result);
                            REQUIRE(*result == (std::numeric_limits<std::int64_t>::min)());
                        }(std::move(buffers->at(1)))
                    );
                });
            }
        }
    }
}

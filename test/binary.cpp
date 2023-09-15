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

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int16_t value = 6789;
                                auto result = co_await asyncio::binary::writeLE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readLE<int16_t>(std::move(buffer));
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

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int16_t value = INT16_MAX;
                                auto result = co_await asyncio::binary::writeLE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readLE<int16_t>(std::move(buffer));
                                REQUIRE(result);
                                REQUIRE(*result == INT16_MAX);
                            }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("min") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int16_t value = INT16_MIN;
                                auto result = co_await asyncio::binary::writeLE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readLE<int16_t>(std::move(buffer));
                                REQUIRE(result);
                                REQUIRE(*result == INT16_MIN);
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

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int32_t value = 6789;
                                auto result = co_await asyncio::binary::writeLE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readLE<int32_t>(std::move(buffer));
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

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int32_t value = INT32_MAX;
                                auto result = co_await asyncio::binary::writeLE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readLE<int32_t>(std::move(buffer));
                                REQUIRE(result);
                                REQUIRE(*result == INT32_MAX);
                            }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("min") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int32_t value = INT32_MIN;
                                auto result = co_await asyncio::binary::writeLE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readLE<int32_t>(std::move(buffer));
                                REQUIRE(result);
                                REQUIRE(*result == INT32_MIN);
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

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int64_t value = 6789;
                                auto result = co_await asyncio::binary::writeLE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readLE<int64_t>(std::move(buffer));
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

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int64_t value = INT64_MAX;
                                auto result = co_await asyncio::binary::writeLE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readLE<int64_t>(std::move(buffer));
                                REQUIRE(result);
                                REQUIRE(*result == INT64_MAX);
                            }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("min") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int64_t value = INT64_MIN;
                                auto result = co_await asyncio::binary::writeLE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readLE<int64_t>(std::move(buffer));
                                REQUIRE(result);
                                REQUIRE(*result == INT64_MIN);
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

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int16_t value = 6789;
                                auto result = co_await asyncio::binary::writeBE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readBE<int16_t>(std::move(buffer));
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

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int16_t value = INT16_MAX;
                                auto result = co_await asyncio::binary::writeBE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readBE<int16_t>(std::move(buffer));
                                REQUIRE(result);
                                REQUIRE(*result == INT16_MAX);
                            }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("min") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int16_t value = INT16_MIN;
                                auto result = co_await asyncio::binary::writeBE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readBE<int16_t>(std::move(buffer));
                                REQUIRE(result);
                                REQUIRE(*result == INT16_MIN);
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

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int32_t value = 6789;
                                auto result = co_await asyncio::binary::writeBE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readBE<int32_t>(std::move(buffer));
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

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int32_t value = INT32_MAX;
                                auto result = co_await asyncio::binary::writeBE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readBE<int32_t>(std::move(buffer));
                                REQUIRE(result);
                                REQUIRE(*result == INT32_MAX);
                            }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("min") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int32_t value = INT32_MIN;
                                auto result = co_await asyncio::binary::writeBE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readBE<int32_t>(std::move(buffer));
                                REQUIRE(result);
                                REQUIRE(*result == INT32_MIN);
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

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int64_t value = 6789;
                                auto result = co_await asyncio::binary::writeBE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readBE<int64_t>(std::move(buffer));
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

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int64_t value = INT64_MAX;
                                auto result = co_await asyncio::binary::writeBE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readBE<int64_t>(std::move(buffer));
                                REQUIRE(result);
                                REQUIRE(*result == INT64_MAX);
                            }(std::move(buffers->at(1)))
                    );
                });
            }

            SECTION("min") {
                asyncio::run([]() -> zero::async::coroutine::Task<void> {
                    auto buffers = asyncio::ev::pipe();
                    REQUIRE(buffers);

                    co_await zero::async::coroutine::allSettled(
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                int64_t value = INT64_MIN;
                                auto result = co_await asyncio::binary::writeBE(std::move(buffer), value);
                                REQUIRE(result);
                            }(std::move(buffers->at(0))),
                            [](auto buffer) -> zero::async::coroutine::Task<void> {
                                auto result = co_await asyncio::binary::readBE<int64_t>(std::move(buffer));
                                REQUIRE(result);
                                REQUIRE(*result == INT64_MIN);
                            }(std::move(buffers->at(1)))
                    );
                });
            }
        }
    }
}
#include <asyncio/buffer.h>
#include <asyncio/event_loop.h>
#include <asyncio/fs/pipe.h>
#include <asyncio/error.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("asyncio buffer", "[buffer]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        auto pipes = asyncio::fs::pipe();
        REQUIRE(pipes);

        SECTION("reader") {
            SECTION("instance") {
                asyncio::BufReader reader(std::move(pipes->at(0)), 16);
                REQUIRE(reader.capacity() == 16);

                SECTION("read") {
                    co_await allSettled(
                        [](auto r) -> zero::async::coroutine::Task<void> {
                            std::byte data[6];
                            auto n = co_await r.read(data);
                            REQUIRE(n);
                            REQUIRE(*n == 6);
                            REQUIRE(memcmp(data, "hello ", 6) == 0);
                            REQUIRE(r.available() == 5);

                            n = co_await r.read(data);
                            REQUIRE(n);
                            REQUIRE(*n == 5);
                            REQUIRE(memcmp(data, "world", 5) == 0);
                            REQUIRE(r.available() == 0);

                            n = co_await r.read(data);
                            REQUIRE(!n);
                            REQUIRE(n.error() == asyncio::Error::IO_EOF);
                        }(std::move(reader)),
                        [](auto pipe) -> zero::async::coroutine::Task<void> {
                            constexpr std::string_view message = "hello world";
                            const auto result = co_await pipe.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(result);
                        }(std::move(pipes->at(1)))
                    );
                }

                SECTION("read line") {
                    co_await allSettled(
                        [](auto r) -> zero::async::coroutine::Task<void> {
                            auto line = co_await r.readLine();
                            REQUIRE(line);
                            REQUIRE(*line == "hello world hello world");

                            line = co_await r.readLine();
                            REQUIRE(line);
                            REQUIRE(*line == "hello hello world hello world");

                            line = co_await r.readLine();
                            REQUIRE(!line);
                            REQUIRE(line.error() == asyncio::Error::IO_EOF);
                        }(std::move(reader)),
                        [](auto pipe) -> zero::async::coroutine::Task<void> {
                            constexpr std::string_view message = "hello world hello world\r\nhello ";
                            auto result = co_await pipe.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(result);

                            result = co_await pipe.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(result);
                        }(std::move(pipes->at(1)))
                    );
                }

                SECTION("read until") {
                    co_await allSettled(
                        [](auto r) -> zero::async::coroutine::Task<void> {
                            auto result = co_await r.readUntil(std::byte{'\1'});
                            REQUIRE(result);
                            REQUIRE(memcmp(result->data(), "hello world hello world", 23) == 0);

                            result = co_await r.readUntil(std::byte{'\1'});
                            REQUIRE(result);
                            REQUIRE(memcmp(result->data(), "hello hello world hello world", 29) == 0);

                            result = co_await r.readUntil(std::byte{'\1'});
                            REQUIRE(!result);
                            REQUIRE(result.error() == asyncio::Error::IO_EOF);
                        }(std::move(reader)),
                        [](auto pipe) -> zero::async::coroutine::Task<void> {
                            constexpr std::string_view message = "hello world hello world\1hello ";
                            auto result = co_await pipe.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(result);

                            result = co_await pipe.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(result);
                        }(std::move(pipes->at(1)))
                    );
                }

                SECTION("peek") {
                    SECTION("normal") {
                        co_await allSettled(
                            [](auto r) -> zero::async::coroutine::Task<void> {
                                std::byte data[6];
                                auto result = co_await r.peek(data);
                                REQUIRE(result);
                                REQUIRE(memcmp(data, "hello ", 6) == 0);
                                REQUIRE(r.available() == 11);

                                result = co_await r.peek(data);
                                REQUIRE(result);
                                REQUIRE(memcmp(data, "hello", 5) == 0);
                                REQUIRE(r.available() == 11);
                            }(std::move(reader)),
                            [](auto pipe) -> zero::async::coroutine::Task<void> {
                                constexpr std::string_view message = "hello world";
                                const auto result = co_await pipe.writeAll(std::as_bytes(std::span{message}));
                                REQUIRE(result);
                            }(std::move(pipes->at(1)))
                        );
                    }

                    SECTION("invalid argument") {
                        std::byte data[17];
                        const auto result = co_await reader.peek(data);
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::invalid_argument);
                    }
                }
            }

            SECTION("unique pointer") {
                asyncio::BufReader reader(std::make_unique<asyncio::fs::Pipe>(std::move(pipes->at(0))));

                co_await allSettled(
                    [](auto r) -> zero::async::coroutine::Task<void> {
                        std::byte data[6];
                        auto n = co_await r.read(data);
                        REQUIRE(n);
                        REQUIRE(*n == 6);
                        REQUIRE(memcmp(data, "hello ", 6) == 0);
                        REQUIRE(r.available() == 5);

                        n = co_await r.read(data);
                        REQUIRE(n);
                        REQUIRE(*n == 5);
                        REQUIRE(memcmp(data, "world", 5) == 0);
                        REQUIRE(r.available() == 0);

                        n = co_await r.read(data);
                        REQUIRE(!n);
                        REQUIRE(n.error() == asyncio::Error::IO_EOF);
                    }(std::move(reader)),
                    [](auto pipe) -> zero::async::coroutine::Task<void> {
                        constexpr std::string_view message = "hello world";
                        const auto result = co_await pipe.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);
                    }(std::move(pipes->at(1)))
                );
            }

            SECTION("shared pointer") {
                asyncio::BufReader reader(std::make_shared<asyncio::fs::Pipe>(std::move(pipes->at(0))));

                co_await allSettled(
                    [](auto r) -> zero::async::coroutine::Task<void> {
                        std::byte data[6];
                        auto n = co_await r.read(data);
                        REQUIRE(n);
                        REQUIRE(*n == 6);
                        REQUIRE(memcmp(data, "hello ", 6) == 0);
                        REQUIRE(r.available() == 5);

                        n = co_await r.read(data);
                        REQUIRE(n);
                        REQUIRE(*n == 5);
                        REQUIRE(memcmp(data, "world", 5) == 0);
                        REQUIRE(r.available() == 0);

                        n = co_await r.read(data);
                        REQUIRE(!n);
                        REQUIRE(n.error() == asyncio::Error::IO_EOF);
                    }(std::move(reader)),
                    [](auto pipe) -> zero::async::coroutine::Task<void> {
                        constexpr std::string_view message = "hello world";
                        const auto result = co_await pipe.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);
                    }(std::move(pipes->at(1)))
                );
            }
        }

        SECTION("writer") {
            SECTION("instance") {
                asyncio::BufWriter writer(std::move(pipes->at(1)), 16);
                REQUIRE(writer.capacity() == 16);

                co_await allSettled(
                    [](auto pipe) -> zero::async::coroutine::Task<void> {
                        std::byte data[11];
                        auto result = co_await pipe.readExactly(data);
                        REQUIRE(result);
                        REQUIRE(memcmp(data, "hello world", 11) == 0);

                        result = co_await pipe.readExactly(data);
                        REQUIRE(result);
                        REQUIRE(memcmp(data, "hello world", 11) == 0);

                        const auto n = co_await pipe.read(data);
                        REQUIRE(!n);
                        REQUIRE(n.error() == asyncio::Error::IO_EOF);
                    }(std::move(pipes->at(0))),
                    [](auto w) -> zero::async::coroutine::Task<void> {
                        constexpr std::string_view message = "hello world";
                        auto result = co_await w.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);
                        REQUIRE(w.pending() == message.size());

                        result = co_await w.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);
                        REQUIRE(w.pending() == message.size() * 2 - 16);

                        result = co_await w.flush();
                        REQUIRE(result);
                    }(std::move(writer))
                );
            }

            SECTION("unique pointer") {
                asyncio::BufWriter writer(std::make_unique<asyncio::fs::Pipe>(std::move(pipes->at(1))), 16);
                REQUIRE(writer.capacity() == 16);

                co_await allSettled(
                    [](auto pipe) -> zero::async::coroutine::Task<void> {
                        std::byte data[11];
                        auto result = co_await pipe.readExactly(data);
                        REQUIRE(result);
                        REQUIRE(memcmp(data, "hello world", 11) == 0);

                        result = co_await pipe.readExactly(data);
                        REQUIRE(result);
                        REQUIRE(memcmp(data, "hello world", 11) == 0);

                        const auto n = co_await pipe.read(data);
                        REQUIRE(!n);
                        REQUIRE(n.error() == asyncio::Error::IO_EOF);
                    }(std::move(pipes->at(0))),
                    [](auto w) -> zero::async::coroutine::Task<void> {
                        constexpr std::string_view message = "hello world";
                        auto result = co_await w.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);
                        REQUIRE(w.pending() == message.size());

                        result = co_await w.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);
                        REQUIRE(w.pending() == message.size() * 2 - 16);

                        result = co_await w.flush();
                        REQUIRE(result);
                    }(std::move(writer))
                );
            }

            SECTION("shared pointer") {
                asyncio::BufWriter writer(std::make_unique<asyncio::fs::Pipe>(std::move(pipes->at(1))), 16);
                REQUIRE(writer.capacity() == 16);

                co_await allSettled(
                    [](auto pipe) -> zero::async::coroutine::Task<void> {
                        std::byte data[11];
                        auto result = co_await pipe.readExactly(data);
                        REQUIRE(result);
                        REQUIRE(memcmp(data, "hello world", 11) == 0);

                        result = co_await pipe.readExactly(data);
                        REQUIRE(result);
                        REQUIRE(memcmp(data, "hello world", 11) == 0);

                        const auto n = co_await pipe.read(data);
                        REQUIRE(!n);
                        REQUIRE(n.error() == asyncio::Error::IO_EOF);
                    }(std::move(pipes->at(0))),
                    [](auto w) -> zero::async::coroutine::Task<void> {
                        constexpr std::string_view message = "hello world";
                        auto result = co_await w.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);
                        REQUIRE(w.pending() == message.size());

                        result = co_await w.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);
                        REQUIRE(w.pending() == message.size() * 2 - 16);

                        result = co_await w.flush();
                        REQUIRE(result);
                    }(std::move(writer))
                );
            }
        }
    });
}

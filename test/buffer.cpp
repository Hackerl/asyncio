#include <asyncio/buffer.h>
#include <asyncio/pipe.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("asyncio buffer", "[buffer]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        auto pipes = asyncio::pipe();
        REQUIRE(pipes);

        SECTION("reader") {
            SECTION("instance") {
                asyncio::BufReader reader(std::move(pipes->at(0)), 16);
                REQUIRE(reader.capacity() == 16);

                SECTION("read") {
                    co_await allSettled(
                        [](auto r) -> asyncio::task::Task<void> {
                            std::string message;
                            message.resize(6);

                            auto res = co_await r.readExactly(std::as_writable_bytes(std::span{message}));
                            REQUIRE(res);
                            REQUIRE(message == "hello ");
                            REQUIRE(r.available() == 5);

                            message.resize(5);
                            res = co_await r.readExactly(std::as_writable_bytes(std::span{message}));
                            REQUIRE(res);
                            REQUIRE(message == "world");
                            REQUIRE(r.available() == 0);

                            auto n = co_await r.read(std::as_writable_bytes(std::span{message}));
                            REQUIRE(n);
                            REQUIRE(*n == 0);
                        }(std::move(reader)),
                        [](auto writer) -> asyncio::task::Task<void> {
                            using namespace std::string_view_literals;
                            const auto res = co_await writer.writeAll(std::as_bytes(std::span{"hello world"sv}));
                            REQUIRE(res);
                        }(std::move(pipes->at(1)))
                    );
                }

                SECTION("read line") {
                    co_await allSettled(
                        [](auto r) -> asyncio::task::Task<void> {
                            auto line = co_await r.readLine();
                            REQUIRE(line);
                            REQUIRE(*line == "hello world hello world");

                            line = co_await r.readLine();
                            REQUIRE(line);
                            REQUIRE(*line == "hello hello world hello world");

                            line = co_await r.readLine();
                            REQUIRE(!line);
                            REQUIRE(line.error() == asyncio::IOError::UNEXPECTED_EOF);
                        }(std::move(reader)),
                        [](auto writer) -> asyncio::task::Task<void> {
                            constexpr std::string_view message = "hello world hello world\r\nhello ";

                            auto res = co_await writer.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(res);

                            res = co_await writer.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(res);
                        }(std::move(pipes->at(1)))
                    );
                }

                SECTION("read until") {
                    co_await allSettled(
                        [](auto r) -> asyncio::task::Task<void> {
                            using namespace std::string_view_literals;

                            auto res = co_await r.readUntil(std::byte{'\1'});
                            REQUIRE(res);
                            REQUIRE(
                                std::string_view{reinterpret_cast<const char *>(res->data()), res->size()}
                                == "hello world hello world"
                            );

                            res = co_await r.readUntil(std::byte{'\1'});
                            REQUIRE(res);
                            REQUIRE(
                                std::string_view{reinterpret_cast<const char *>(res->data()), res->size()}
                                == "hello hello world hello world"
                            );

                            res = co_await r.readUntil(std::byte{'\1'});
                            REQUIRE(!res);
                            REQUIRE(res.error() == asyncio::IOError::UNEXPECTED_EOF);
                        }(std::move(reader)),
                        [](auto writer) -> asyncio::task::Task<void> {
                            constexpr std::string_view message = "hello world hello world\1hello ";

                            auto res = co_await writer.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(res);

                            res = co_await writer.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(res);
                        }(std::move(pipes->at(1)))
                    );
                }

                SECTION("peek") {
                    SECTION("normal") {
                        co_await allSettled(
                            [](auto r) -> asyncio::task::Task<void> {
                                std::string message;
                                message.resize(6);

                                auto res = co_await r.peek(std::as_writable_bytes(std::span{message}));
                                REQUIRE(res);
                                REQUIRE(message == "hello ");
                                REQUIRE(r.available() == 11);

                                res = co_await r.peek(std::as_writable_bytes(std::span{message}));
                                REQUIRE(res);
                                REQUIRE(message == "hello ");
                                REQUIRE(r.available() == 11);
                            }(std::move(reader)),
                            [](auto writer) -> asyncio::task::Task<void> {
                                using namespace std::string_view_literals;
                                const auto res = co_await writer.writeAll(std::as_bytes(std::span{"hello world"sv}));
                                REQUIRE(res);
                            }(std::move(pipes->at(1)))
                        );
                    }

                    SECTION("invalid argument") {
                        std::array<std::byte, 17> data{};
                        const auto res = co_await reader.peek(data);
                        REQUIRE(!res);
                        REQUIRE(res.error() == std::errc::invalid_argument);
                    }
                }
            }

            SECTION("unique pointer") {
                asyncio::BufReader reader(std::make_unique<asyncio::Pipe>(std::move(pipes->at(0))));

                co_await allSettled(
                    [](auto r) -> asyncio::task::Task<void> {
                        std::string message;
                        message.resize(6);

                        auto res = co_await r.readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == "hello ");
                        REQUIRE(r.available() == 5);

                        message.resize(5);
                        res = co_await r.readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == "world");
                        REQUIRE(r.available() == 0);

                        auto n = co_await r.read(std::as_writable_bytes(std::span{message}));
                        REQUIRE(n);
                        REQUIRE(*n == 0);
                    }(std::move(reader)),
                    [](auto writer) -> asyncio::task::Task<void> {
                        using namespace std::string_view_literals;
                        const auto res = co_await writer.writeAll(std::as_bytes(std::span{"hello world"sv}));
                        REQUIRE(res);
                    }(std::move(pipes->at(1)))
                );
            }

            SECTION("shared pointer") {
                asyncio::BufReader reader(std::make_shared<asyncio::Pipe>(std::move(pipes->at(0))));

                co_await allSettled(
                    [](auto r) -> asyncio::task::Task<void> {
                        std::string message;
                        message.resize(6);

                        auto res = co_await r.readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == "hello ");
                        REQUIRE(r.available() == 5);

                        message.resize(5);
                        res = co_await r.readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == "world");
                        REQUIRE(r.available() == 0);

                        auto n = co_await r.read(std::as_writable_bytes(std::span{message}));
                        REQUIRE(n);
                        REQUIRE(*n == 0);
                    }(std::move(reader)),
                    [](auto writer) -> asyncio::task::Task<void> {
                        using namespace std::string_view_literals;
                        const auto res = co_await writer.writeAll(std::as_bytes(std::span{"hello world"sv}));
                        REQUIRE(res);
                    }(std::move(pipes->at(1)))
                );
            }
        }

        SECTION("writer") {
            SECTION("instance") {
                asyncio::BufWriter writer(std::move(pipes->at(1)), 16);
                REQUIRE(writer.capacity() == 16);

                co_await allSettled(
                    [](auto reader) -> asyncio::task::Task<void> {
                        std::string message;
                        message.resize(11);

                        auto res = co_await reader.readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == "hello world");

                        res = co_await reader.readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == "hello world");

                        const auto n = co_await reader.read(std::as_writable_bytes(std::span{message}));
                        REQUIRE(n);
                        REQUIRE(*n == 0);
                    }(std::move(pipes->at(0))),
                    [](auto w) -> asyncio::task::Task<void> {
                        constexpr std::string_view message = "hello world";
                        auto res = co_await w.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(w.pending() == message.size());

                        res = co_await w.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(w.pending() == message.size() * 2 - 16);

                        res = co_await w.flush();
                        REQUIRE(res);
                    }(std::move(writer))
                );
            }

            SECTION("unique pointer") {
                asyncio::BufWriter writer(std::make_unique<asyncio::Pipe>(std::move(pipes->at(1))), 16);
                REQUIRE(writer.capacity() == 16);

                co_await allSettled(
                    [](auto reader) -> asyncio::task::Task<void> {
                        std::string message;
                        message.resize(11);

                        auto res = co_await reader.readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == "hello world");

                        res = co_await reader.readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == "hello world");

                        const auto n = co_await reader.read(std::as_writable_bytes(std::span{message}));
                        REQUIRE(n);
                        REQUIRE(*n == 0);
                    }(std::move(pipes->at(0))),
                    [](auto w) -> asyncio::task::Task<void> {
                        constexpr std::string_view message = "hello world";
                        auto res = co_await w.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(w.pending() == message.size());

                        res = co_await w.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(w.pending() == message.size() * 2 - 16);

                        res = co_await w.flush();
                        REQUIRE(res);
                    }(std::move(writer))
                );
            }

            SECTION("shared pointer") {
                asyncio::BufWriter writer(std::make_unique<asyncio::Pipe>(std::move(pipes->at(1))), 16);
                REQUIRE(writer.capacity() == 16);

                co_await allSettled(
                    [](auto reader) -> asyncio::task::Task<void> {
                        std::string message;
                        message.resize(11);

                        auto res = co_await reader.readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == "hello world");

                        res = co_await reader.readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == "hello world");

                        const auto n = co_await reader.read(std::as_writable_bytes(std::span{message}));
                        REQUIRE(n);
                        REQUIRE(*n == 0);
                    }(std::move(pipes->at(0))),
                    [](auto w) -> asyncio::task::Task<void> {
                        constexpr std::string_view message = "hello world";
                        auto res = co_await w.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(w.pending() == message.size());

                        res = co_await w.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(w.pending() == message.size() * 2 - 16);

                        res = co_await w.flush();
                        REQUIRE(res);
                    }(std::move(writer))
                );
            }
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

#include <asyncio/io.h>
#include <asyncio/ev/pipe.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("asynchronous io", "[io]") {
    SECTION("copy") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto buffers1 = asyncio::ev::pipe();
            REQUIRE(buffers1);

            auto buffers2 = asyncio::ev::pipe();
            REQUIRE(buffers2);

            co_await zero::async::coroutine::allSettled(
                    [](auto reader, auto writer) -> zero::async::coroutine::Task<void> {
                        auto result = co_await asyncio::copy(reader, writer);
                        REQUIRE(result);
                    }(std::move(buffers1->at(1)), std::move(buffers2->at(0))),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        std::string message = "hello world\r\n";
                        auto result = co_await buffer.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);

                        result = co_await buffer.flush();
                        REQUIRE(result);
                    }(std::move(buffers1->at(0))),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        auto result = co_await buffer.readLine();

                        REQUIRE(result);
                        REQUIRE(*result == "hello world");

                        result = co_await buffer.readLine();
                        REQUIRE(!result);
                        REQUIRE(result.error() == asyncio::Error::IO_EOF);
                    }(std::move(buffers2->at(1)))
            );
        });
    }

    SECTION("read all") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto buffers = asyncio::ev::pipe();
            REQUIRE(buffers);

            co_await zero::async::coroutine::allSettled(
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        std::string message = "hello world\r\n";
                        auto result = co_await buffer.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);

                        result = co_await buffer.flush();
                        REQUIRE(result);

                        message = "world hello\r\n";
                        result = co_await buffer.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);

                        result = co_await buffer.flush();
                        REQUIRE(result);
                    }(std::move(buffers->at(0))),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        auto result = co_await buffer.readAll();
                        REQUIRE(result);
                        REQUIRE(result->size() == 26);
                        REQUIRE(memcmp(result->data(), "hello world\r\nworld hello\r\n", 26) == 0);
                    }(std::move(buffers->at(1)))
            );
        });
    }

    SECTION("read exactly") {
        SECTION("normal") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto buffers = asyncio::ev::pipe();
                REQUIRE(buffers);

                co_await zero::async::coroutine::allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            std::string message = "hello world\r\n";
                            auto result = co_await buffer.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(result);

                            result = co_await buffer.flush();
                            REQUIRE(result);

                            message = "world hello\r\n";
                            result = co_await buffer.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(result);

                            result = co_await buffer.flush();
                            REQUIRE(result);

                            message = "hello world\r\n";
                            result = co_await buffer.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(result);

                            result = co_await buffer.flush();
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            std::byte data[39];
                            auto result = co_await buffer.readExactly(data);
                            REQUIRE(result);
                            REQUIRE(memcmp(data, "hello world\r\nworld hello\r\nhello world\r\n", 39) == 0);
                        }(std::move(buffers->at(1)))
                );
            });
        }

        SECTION("error") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto buffers = asyncio::ev::pipe();
                REQUIRE(buffers);

                co_await zero::async::coroutine::allSettled(
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            std::string message = "hello world\r\n";
                            auto result = co_await buffer.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(result);

                            result = co_await buffer.flush();
                            REQUIRE(result);

                            message = "world hello\r\n";
                            result = co_await buffer.writeAll(std::as_bytes(std::span{message}));
                            REQUIRE(result);

                            result = co_await buffer.flush();
                            REQUIRE(result);
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            std::byte data[39];
                            auto result = co_await buffer.readExactly(data);
                            REQUIRE(!result);
                            REQUIRE(result.error() == asyncio::Error::IO_EOF);
                        }(std::move(buffers->at(1)))
                );
            });
        }
    }
}
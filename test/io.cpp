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
                        writer->close();
                    }(std::make_shared<asyncio::ev::PairedBuffer>(std::move(buffers1->at(1))),
                      std::make_shared<asyncio::ev::PairedBuffer>(std::move(buffers2->at(0)))),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        buffer.writeLine("hello world");
                        co_await buffer.drain();
                        buffer.close();
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
                        buffer.writeLine("hello world");
                        co_await buffer.drain();

                        buffer.writeLine("world hello");
                        co_await buffer.drain();

                        buffer.close();
                    }(std::move(buffers->at(0))),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        auto result = co_await asyncio::readAll(std::move(buffer));
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
                            buffer.writeLine("hello world");
                            co_await buffer.drain();

                            buffer.writeLine("world hello");
                            co_await buffer.drain();

                            buffer.writeLine("hello world");
                            co_await buffer.drain();

                            buffer.close();
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            std::byte data[39];
                            auto result = co_await asyncio::readExactly(std::move(buffer), data);
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
                            buffer.writeLine("hello world");
                            co_await buffer.drain();

                            buffer.writeLine("world hello");
                            co_await buffer.drain();

                            buffer.close();
                        }(std::move(buffers->at(0))),
                        [](auto buffer) -> zero::async::coroutine::Task<void> {
                            std::byte data[39];
                            auto result = co_await asyncio::readExactly(std::move(buffer), data);
                            REQUIRE(!result);
                            REQUIRE(result.error() == asyncio::Error::IO_EOF);
                        }(std::move(buffers->at(1)))
                );
            });
        }
    }
}
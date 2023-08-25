#include <asyncio/io.h>
#include <asyncio/error.h>
#include <asyncio/ev/pipe.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("asynchronous io", "[io]") {
    SECTION("copy") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto buffers1 = asyncio::ev::pipe().transform([](std::array<asyncio::ev::PairedBuffer, 2> &&buffers) {
                return std::array{
                        std::make_shared<asyncio::ev::PairedBuffer>(std::move(buffers[0])),
                        std::make_shared<asyncio::ev::PairedBuffer>(std::move(buffers[1]))
                };
            });

            REQUIRE(buffers1);

            auto buffers2 = asyncio::ev::pipe().transform([](std::array<asyncio::ev::PairedBuffer, 2> &&buffers) {
                return std::array{
                        std::make_shared<asyncio::ev::PairedBuffer>(std::move(buffers[0])),
                        std::make_shared<asyncio::ev::PairedBuffer>(std::move(buffers[1]))
                };
            });

            REQUIRE(buffers2);

            co_await zero::async::coroutine::allSettled(
                    [](auto reader, auto writer) -> zero::async::coroutine::Task<void> {
                        auto result = co_await asyncio::copy(reader, writer);
                        REQUIRE(result);
                        writer->close();
                    }(buffers1->at(1), buffers2->at(0)),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        buffer->writeLine("hello world");
                        co_await buffer->drain();
                        buffer->close();
                    }(buffers1->at(0)),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        auto result = co_await buffer->readLine();

                        REQUIRE(result);
                        REQUIRE(*result == "hello world");

                        result = co_await buffer->readLine();
                        REQUIRE(!result);
                        REQUIRE(result.error() == asyncio::Error::IO_EOF);
                    }(buffers2->at(1))
            );
        });
    }

    SECTION("read all") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto buffers = asyncio::ev::pipe().transform([](std::array<asyncio::ev::PairedBuffer, 2> &&buffers) {
                return std::array{
                        std::make_shared<asyncio::ev::PairedBuffer>(std::move(buffers[0])),
                        std::make_shared<asyncio::ev::PairedBuffer>(std::move(buffers[1]))
                };
            });

            REQUIRE(buffers);

            co_await zero::async::coroutine::allSettled(
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        buffer->writeLine("hello world");
                        co_await buffer->drain();

                        buffer->writeLine("world hello");
                        co_await buffer->drain();

                        buffer->close();
                    }(buffers->at(0)),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        auto result = co_await asyncio::readAll(buffer);
                        REQUIRE(result);
                        REQUIRE(result->size() == 26);
                        REQUIRE(memcmp(result->data(), "hello world\r\nworld hello\r\n", 26) == 0);
                    }(buffers->at(1))
            );
        });
    }
}
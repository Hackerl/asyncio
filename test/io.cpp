#include <asyncio/io.h>
#include <asyncio/error.h>
#include <asyncio/ev/pipe.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("asynchronous io", "[io]") {
    SECTION("copy") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto buffers1 = asyncio::ev::pipe();
            auto buffers2 = asyncio::ev::pipe();
            REQUIRE(buffers1);
            REQUIRE(buffers2);

            co_await zero::async::coroutine::allSettled(
                    [&]() -> zero::async::coroutine::Task<void> {
                        auto result = co_await asyncio::copy(buffers1->at(1), buffers2->at(0));
                        REQUIRE(result);
                        buffers2->at(0).close();
                    }(),
                    [&buffer = buffers1->at(0)]() -> zero::async::coroutine::Task<void> {
                        buffer.writeLine("hello world");
                        co_await buffer.drain();
                        buffer.close();
                    }(),
                    [&buffer = buffers2->at(1)]() -> zero::async::coroutine::Task<void> {
                        auto result = co_await buffer.readLine();

                        REQUIRE(result);
                        REQUIRE(*result == "hello world");

                        result = co_await buffer.readLine();
                        REQUIRE(!result);
                        REQUIRE(result.error() == asyncio::Error::IO_EOF);
                    }()
            );
        });
    }

    SECTION("read all") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto buffers = asyncio::ev::pipe();
            REQUIRE(buffers);

            co_await zero::async::coroutine::allSettled(
                    [&buffer = buffers->at(0)]() -> zero::async::coroutine::Task<void> {
                        buffer.writeLine("hello world");
                        co_await buffer.drain();

                        buffer.writeLine("world hello");
                        co_await buffer.drain();

                        buffer.close();
                    }(),
                    [&buffer = buffers->at(1)]() -> zero::async::coroutine::Task<void> {
                        auto result = co_await asyncio::readAll(buffer);
                        REQUIRE(result);
                        REQUIRE(result->size() == 26);
                        REQUIRE(memcmp(result->data(), "hello world\r\nworld hello\r\n", 26) == 0);
                    }()
            );
        });
    }
}
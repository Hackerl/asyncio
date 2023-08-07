#include <asyncio/ev/pipe.h>
#include <asyncio/event_loop.h>
#include <asyncio/error.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("buffer pipe", "[pipe]") {
    SECTION("normal") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto buffers = asyncio::ev::pipe();
            REQUIRE(buffers);

            co_await zero::async::coroutine::all(
                    [&buffer = buffers->at(0)]() -> zero::async::coroutine::Task<void> {
                        buffer.writeLine("hello world");
                        co_await buffer.drain();

                        auto result = co_await buffer.readLine();

                        REQUIRE(result);
                        REQUIRE(*result == "world hello");

                        buffer.close();
                    }(),
                    [&buffer = buffers->at(1)]() -> zero::async::coroutine::Task<void> {
                        auto result = co_await buffer.readLine();

                        REQUIRE(result);
                        REQUIRE(*result == "hello world");

                        buffer.writeLine("world hello");

                        co_await buffer.drain();
                        result = co_await buffer.readLine();

                        REQUIRE(!result);
                        REQUIRE(result.error() == asyncio::Error::IO_EOF);
                    }()
            );
        });
    }

    SECTION("throws error") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto buffers = asyncio::ev::pipe();
            REQUIRE(buffers);

            co_await zero::async::coroutine::all(
                    [&buffer = buffers->at(0)]() -> zero::async::coroutine::Task<void> {
                        buffer.writeLine("hello world");
                        co_await buffer.drain();

                        auto result = co_await buffer.readLine();

                        REQUIRE(result);
                        REQUIRE(*result == "world hello");

                        buffer.throws(make_error_code(std::errc::interrupted));
                    }(),
                    [&buffer = buffers->at(1)]() -> zero::async::coroutine::Task<void> {
                        auto result = co_await buffer.readLine();

                        REQUIRE(result);
                        REQUIRE(*result == "hello world");

                        buffer.writeLine("world hello");

                        co_await buffer.drain();
                        result = co_await buffer.readLine();

                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::interrupted);
                    }()
            );
        });
    }
}
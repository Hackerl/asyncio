#include <asyncio/ev/pipe.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("buffer pipe", "[pipe]") {
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

                        auto line = co_await buffer.readLine();

                        REQUIRE(line);
                        REQUIRE(*line == "world hello");
                    }(std::move(buffers->at(0))),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        auto line = co_await buffer.readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "hello world");

                        std::string message = "world hello\r\n";
                        auto result = co_await buffer.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);

                        result = co_await buffer.flush();
                        REQUIRE(result);

                        line = co_await buffer.readLine();

                        REQUIRE(!line);
                        REQUIRE(line.error() == asyncio::Error::IO_EOF);
                    }(std::move(buffers->at(1)))
            );
        });
    }

    SECTION("throws error") {
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

                        auto line = co_await buffer.readLine();

                        REQUIRE(line);
                        REQUIRE(*line == "world hello");

                        buffer.throws(make_error_code(std::errc::interrupted));
                    }(std::move(buffers->at(0))),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        auto line = co_await buffer.readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "hello world");

                        std::string message = "world hello\r\n";
                        auto result = co_await buffer.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);

                        result = co_await buffer.flush();
                        REQUIRE(result);

                        line = co_await buffer.readLine();
                        REQUIRE(!line);
                        REQUIRE(line.error() == std::errc::interrupted);
                    }(std::move(buffers->at(1)))
            );
        });
    }
}
#include <asyncio/ev/pipe.h>
#include <asyncio/event_loop.h>
#include <asyncio/error.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("buffer pipe", "[pipe]") {
    SECTION("normal") {
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

                        auto result = co_await buffer->readLine();

                        REQUIRE(result);
                        REQUIRE(*result == "world hello");

                        buffer->close();
                    }(buffers->at(0)),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        auto result = co_await buffer->readLine();

                        REQUIRE(result);
                        REQUIRE(*result == "hello world");

                        buffer->writeLine("world hello");

                        co_await buffer->drain();
                        result = co_await buffer->readLine();

                        REQUIRE(!result);
                        REQUIRE(result.error() == asyncio::Error::IO_EOF);
                    }(buffers->at(1))
            );
        });
    }

    SECTION("throws error") {
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

                        auto result = co_await buffer->readLine();

                        REQUIRE(result);
                        REQUIRE(*result == "world hello");

                        buffer->throws(make_error_code(std::errc::interrupted));
                    }(buffers->at(0)),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        auto result = co_await buffer->readLine();

                        REQUIRE(result);
                        REQUIRE(*result == "hello world");

                        buffer->writeLine("world hello");

                        co_await buffer->drain();
                        result = co_await buffer->readLine();

                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::interrupted);
                    }(buffers->at(1))
            );
        });
    }
}
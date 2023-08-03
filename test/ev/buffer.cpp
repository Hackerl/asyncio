#include <asyncio/ev/buffer.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("async stream buffer", "[buffer]") {
    evutil_socket_t fds[2];

#ifdef _WIN32
    REQUIRE(evutil_socketpair(AF_INET, SOCK_STREAM, 0, fds) == 0);
#else
    REQUIRE(evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
#endif

    SECTION("normal") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            co_await zero::async::coroutine::all(
                    [&]() -> zero::async::coroutine::Task<void> {
                        std::shared_ptr<asyncio::ev::IBuffer> buffer = asyncio::ev::newBuffer(fds[0]);

                        REQUIRE(buffer);
                        REQUIRE(buffer->fd() > 0);

                        buffer->writeLine("hello world");
                        co_await buffer->drain();

                        auto result = co_await buffer->readLine();

                        REQUIRE(result);
                        REQUIRE(result.value() == "world hello");

                        buffer->close();
                    }(),
                    [&]() -> zero::async::coroutine::Task<void> {
                        std::shared_ptr<asyncio::ev::IBuffer> buffer = asyncio::ev::newBuffer(fds[1]);

                        REQUIRE(buffer);
                        REQUIRE(buffer->fd() > 0);

                        auto result = co_await buffer->readLine();

                        REQUIRE(result);
                        REQUIRE(result.value() == "hello world");

                        buffer->writeLine("world hello");

                        co_await buffer->drain();
                        co_await buffer->waitClosed();
                    }()
            );
        });
    }

    SECTION("read timeout") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            std::shared_ptr<asyncio::ev::IBuffer> buffer = asyncio::ev::newBuffer(fds[0]);

            REQUIRE(buffer);
            REQUIRE(buffer->fd() > 0);

            buffer->setTimeout(50ms, 0ms);

            std::byte data[10240];
            auto result = co_await buffer->read(data);

            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        });
    }

    SECTION("write timeout") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            std::shared_ptr<asyncio::ev::IBuffer> buffer = asyncio::ev::newBuffer(fds[0]);

            REQUIRE(buffer);
            REQUIRE(buffer->fd() > 0);

            buffer->setTimeout(0ms, 500ms);

            std::unique_ptr<std::byte[]> data = std::make_unique<std::byte[]>(1024 * 1024);

            buffer->submit({data.get(), 1024 * 1024});
            auto result = co_await buffer->drain();

            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        });
    }
}
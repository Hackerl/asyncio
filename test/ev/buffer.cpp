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

    REQUIRE(evutil_make_socket_nonblocking(fds[0]) != -1);
    REQUIRE(evutil_make_socket_nonblocking(fds[1]) != -1);

    SECTION("normal") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            co_await zero::async::coroutine::allSettled(
                    [&]() -> zero::async::coroutine::Task<void> {
                        auto buffer = asyncio::ev::makeBuffer(fds[0]);

                        REQUIRE(buffer);
                        REQUIRE(buffer->fd() > 0);

                        buffer->writeLine("hello world");
                        co_await buffer->drain();

                        auto result = co_await buffer->readLine();

                        REQUIRE(result);
                        REQUIRE(*result == "world hello");

                        buffer->close();
                    }(),
                    [&]() -> zero::async::coroutine::Task<void> {
                        auto buffer = asyncio::ev::makeBuffer(fds[1]);

                        REQUIRE(buffer);
                        REQUIRE(buffer->fd() > 0);

                        auto result = co_await buffer->readLine();

                        REQUIRE(result);
                        REQUIRE(*result == "hello world");

                        buffer->writeLine("world hello");

                        co_await buffer->drain();
                        co_await buffer->waitClosed();
                    }()
            );
        });
    }

    SECTION("read timeout") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto buffer = asyncio::ev::makeBuffer(fds[0]);

            REQUIRE(buffer);
            REQUIRE(buffer->fd() > 0);

            buffer->setTimeout(50ms, 0ms);

            std::byte data[10240];
            auto result = co_await buffer->read(data);

            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        });

        evutil_closesocket(fds[1]);
    }

    SECTION("write timeout") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto buffer = asyncio::ev::makeBuffer(fds[0]);

            REQUIRE(buffer);
            REQUIRE(buffer->fd() > 0);

            buffer->setTimeout(0ms, 500ms);

            std::unique_ptr<std::byte[]> data = std::make_unique<std::byte[]>(1024 * 1024);

            buffer->submit({data.get(), 1024 * 1024});
            auto result = co_await buffer->drain();

            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        });

        evutil_closesocket(fds[1]);
    }

    SECTION("cancel") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto buffer = asyncio::ev::makeBuffer(fds[0]);

            REQUIRE(buffer);
            REQUIRE(buffer->fd() > 0);

            std::byte data[10240];
            auto task = buffer->read(data);
            auto result = co_await asyncio::timeout(task, 50ms);

            REQUIRE(task.done());
            REQUIRE(task.result().error() == std::errc::operation_canceled);
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        });

        evutil_closesocket(fds[1]);
    }
}
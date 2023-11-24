#include <asyncio/ev/event.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("async event notification", "[event]") {
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
            co_await allSettled(
                [](evutil_socket_t fd) -> zero::async::coroutine::Task<void> {
                    auto event = makeEvent(fd, asyncio::ev::What::READ);
                    REQUIRE(event);

                    auto result = co_await event->on();

                    REQUIRE(result);
                    REQUIRE(*result & asyncio::ev::What::READ);

                    char buffer[1024] = {};
                    REQUIRE(recv(fd, buffer, sizeof(buffer), 0) == 11);
                    REQUIRE(strcmp(buffer, "hello world") == 0);

                    result = co_await event->on();
                    REQUIRE(result);
                    REQUIRE(*result & asyncio::ev::What::READ);

                    REQUIRE(recv(fd, buffer, sizeof(buffer), 0) == 0);

                    evutil_closesocket(fd);
                }(fds[0]),
                [](const evutil_socket_t fd) -> zero::async::coroutine::Task<void> {
                    auto event = makeEvent(fd, asyncio::ev::What::WRITE);
                    REQUIRE(event);

                    const auto result = co_await event->on();
                    REQUIRE(result);
                    REQUIRE(*result & asyncio::ev::What::WRITE);

                    REQUIRE(send(fd, "hello world", 11, 0) == 11);

                    evutil_closesocket(fd);
                }(fds[1])
            );
        });
    }

    SECTION("wait timeout") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto event = makeEvent(fds[0], asyncio::ev::What::READ);
            REQUIRE(event);

            const auto result = co_await event->on(50ms);
            REQUIRE(result);
            REQUIRE(*result & asyncio::ev::What::TIMEOUT);
        });

        evutil_closesocket(fds[0]);
        evutil_closesocket(fds[1]);
    }

    SECTION("cancel") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto event = makeEvent(fds[0], asyncio::ev::What::READ);
            REQUIRE(event);

            const auto task = event->on();
            const auto result = co_await asyncio::timeout(task, 50ms);

            REQUIRE(task.done());
            REQUIRE(task.result().error() == std::errc::operation_canceled);
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        });

        evutil_closesocket(fds[0]);
        evutil_closesocket(fds[1]);
    }
}

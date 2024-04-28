#include <asyncio/ev/event.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

constexpr std::string_view MESSAGE = "hello world";

TEST_CASE("async event notification", "[ev]") {
    evutil_socket_t fds[2];

#ifdef _WIN32
    REQUIRE(evutil_socketpair(AF_INET, SOCK_STREAM, 0, fds) == 0);
#else
    REQUIRE(evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
#endif

    REQUIRE(evutil_make_socket_nonblocking(fds[0]) == 0);
    REQUIRE(evutil_make_socket_nonblocking(fds[1]) == 0);

    asyncio::run([&]() -> zero::async::coroutine::Task<void> {
        std::array events = {
            asyncio::ev::Event::make(fds[0], asyncio::ev::What::READ),
            asyncio::ev::Event::make(fds[1], asyncio::ev::What::WRITE)
        };

        REQUIRE(events[0]);
        REQUIRE(events[1]);
        REQUIRE(events[0]->fd() == fds[0]);
        REQUIRE(events[1]->fd() == fds[1]);
        REQUIRE(!events[0]->pending());
        REQUIRE(!events[1]->pending());

        SECTION("notify") {
            co_await allSettled(
                [](auto event) -> zero::async::coroutine::Task<void> {
                    const auto fd = event.fd();
                    const auto result = co_await event.on();
                    REQUIRE(result);
                    REQUIRE(*result & asyncio::ev::What::READ);

                    char buffer[1024] = {};
                    REQUIRE(recv(fd, buffer, sizeof(buffer), 0) == 11);
                    REQUIRE(buffer == MESSAGE);
                }(*std::move(events[0])),
                [](auto event) -> zero::async::coroutine::Task<void> {
                    const auto fd = event.fd();
                    const auto result = co_await event.on();
                    REQUIRE(result);
                    REQUIRE(*result & asyncio::ev::What::WRITE);
                    REQUIRE(send(event.fd(), MESSAGE.data(), MESSAGE.size(), 0) == MESSAGE.size());
                }(*std::move(events[1]))
            );
        }

        SECTION("timeout") {
            const auto result = co_await events[0]->on(10ms);
            REQUIRE(result);
            REQUIRE(*result & asyncio::ev::What::TIMEOUT);
        }

        SECTION("timeout") {
            const auto result = co_await asyncio::timeout(events[0]->on(), 10ms);
            REQUIRE(!result);
            REQUIRE(result.error() == asyncio::TimeoutError::ELAPSED);
        }
    });

    REQUIRE(evutil_closesocket(fds[0]) == 0);
    REQUIRE(evutil_closesocket(fds[1]) == 0);
}

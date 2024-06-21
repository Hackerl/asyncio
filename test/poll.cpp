#include <asyncio/poll.h>
#include <asyncio/time.h>
#include <catch2/catch_test_macros.hpp>

#ifndef _WIN32
#include <unistd.h>
#endif

TEST_CASE("poll events", "[poll]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        std::array<uv_os_sock_t, 2> sockets = {};
        REQUIRE(uv_socketpair(SOCK_STREAM, 0, sockets.data(), UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE) == 0);

        std::array polls = {
            asyncio::Poll::make(sockets[0]),
            asyncio::Poll::make(sockets[1])
        };
        REQUIRE(polls[0]);
        REQUIRE(polls[1]);

        SECTION("normal") {
            co_await allSettled(
                [](auto socket, auto poll) -> asyncio::task::Task<void> {
                    using namespace std::string_view_literals;

                    const auto res = co_await poll.on(asyncio::Poll::Event::READABLE);
                    REQUIRE(res);
                    REQUIRE(*res & asyncio::Poll::Event::READABLE);

                    std::array<char, 1024> buffer = {};
                    REQUIRE(recv(socket, buffer.data(), buffer.size(), 0) == 11);
                    REQUIRE(buffer.data() == "hello world"sv);
                }(sockets[0], *std::move(polls[0])),
                [](auto socket, auto poll) -> asyncio::task::Task<void> {
                    using namespace std::string_view_literals;

                    const auto res = co_await poll.on(asyncio::Poll::Event::WRITABLE);
                    REQUIRE(res);
                    REQUIRE(*res & asyncio::Poll::Event::WRITABLE);

                    constexpr auto message = "hello world"sv;
                    REQUIRE(send(socket, message.data(), message.size(), 0) == message.size());
                }(sockets[1], *std::move(polls[1]))
            );
        }

        SECTION("timeout") {
            using namespace std::chrono_literals;
            const auto res = co_await asyncio::timeout(polls[0]->on(asyncio::Poll::Event::READABLE), 10ms);
            REQUIRE(!res);
            REQUIRE(res.error() == asyncio::TimeoutError::ELAPSED);
        }

#ifdef _WIN32
        REQUIRE(closesocket(sockets[0]) == 0);
        REQUIRE(closesocket(sockets[1]) == 0);
#else
        REQUIRE(close(sockets[0]) == 0);
        REQUIRE(close(sockets[1]) == 0);
#endif
    });
    REQUIRE(result);
    REQUIRE(*result);
}

#include "catch_extensions.h"
#include <asyncio/poll.h>
#include <zero/defer.h>
#include <catch2/catch_test_macros.hpp>

#ifndef _WIN32
#include <unistd.h>
#endif

constexpr std::string_view MESSAGE = "hello world";

ASYNC_TEST_CASE("poll events", "[poll]") {
    std::array<uv_os_sock_t, 2> sockets{};
    REQUIRE(uv_socketpair(SOCK_STREAM, 0, sockets.data(), UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE) == 0);

#ifdef _WIN32
    DEFER(REQUIRE(closesocket(sockets[0]) == 0));
    DEFER(REQUIRE(closesocket(sockets[1]) == 0));
#else
    DEFER(REQUIRE(close(sockets[0]) == 0));
    DEFER(REQUIRE(close(sockets[1]) == 0));
#endif

    auto poll = asyncio::Poll::make(sockets[0]);
    REQUIRE(poll);

    SECTION("readable") {
        REQUIRE(send(sockets[1], MESSAGE.data(), MESSAGE.size(), 0) == MESSAGE.size());

        const auto events = co_await poll->on(asyncio::Poll::Event::READABLE);
        REQUIRE(events);
        REQUIRE(*events & asyncio::Poll::Event::READABLE);

        std::string message;
        message.resize(MESSAGE.size());

        REQUIRE(recv(sockets[0], message.data(), message.size(), 0) == MESSAGE.size());
        REQUIRE(message == MESSAGE);
    }

    SECTION("writable") {
        const auto events = co_await poll->on(asyncio::Poll::Event::WRITABLE);
        REQUIRE(events);
        REQUIRE(*events & asyncio::Poll::Event::WRITABLE);
    }

    SECTION("cancel") {
        auto task = poll->on(asyncio::Poll::Event::READABLE);
        REQUIRE(task.cancel());
        REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
    }
}

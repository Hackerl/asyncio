#include "catch_extensions.h"
#include <asyncio/poll.h>
#include <asyncio/error.h>
#include <zero/defer.h>

#ifndef _WIN32
#include <unistd.h>
#include <zero/os/unix/error.h>
#endif

ASYNC_TEST_CASE("poll events", "[poll]") {
    std::array<uv_os_sock_t, 2> sockets{};

    co_await asyncio::error::guard(asyncio::uv::expected([&] {
        return uv_socketpair(SOCK_STREAM, 0, sockets.data(), UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE);
    }));

#ifdef _WIN32
    Z_DEFER(
        if (closesocket(sockets[0]) != 0)
            throw zero::error::StacktraceError<std::system_error>{WSAGetLastError(), std::system_category()};
    );

    Z_DEFER(
        if (closesocket(sockets[1]) != 0)
            throw zero::error::StacktraceError<std::system_error>{WSAGetLastError(), std::system_category()};
    );
#else
    Z_DEFER(zero::error::guard(zero::os::unix::expected([&] {
        return close(sockets[0]);
    })));

    Z_DEFER(zero::error::guard(zero::os::unix::expected([&] {
        return close(sockets[1]);
    })));
#endif

    auto poll = asyncio::Poll::make(sockets[0]);

    SECTION("readable") {
        if (send(sockets[1], "hello world", 11, 0) != 11)
            throw co_await asyncio::error::StacktraceError<std::runtime_error>::make("Failed to send data");

        const auto events = co_await poll.on(asyncio::Poll::Event::Readable);
        REQUIRE(events);
        REQUIRE(*events & asyncio::Poll::Event::Readable);
    }

    SECTION("writable") {
        const auto events = co_await poll.on(asyncio::Poll::Event::Writable);
        REQUIRE(events);
        REQUIRE(*events & asyncio::Poll::Event::Writable);
    }

    SECTION("cancel") {
        auto task = poll.on(asyncio::Poll::Event::Readable);
        REQUIRE(task.cancel());
        REQUIRE_ERROR(co_await task, std::errc::operation_canceled);
    }
}

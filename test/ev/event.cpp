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

    SECTION("normal") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            co_await zero::async::coroutine::all(
                    [&]() -> zero::async::coroutine::Task<void> {
                        asyncio::ev::Event event(fds[0]);

                        auto result = co_await event.on(asyncio::ev::What::READ);

                        REQUIRE(result);
                        REQUIRE(result.value() & asyncio::ev::What::READ);

                        char buffer[1024] = {};
                        REQUIRE(recv(fds[0], buffer, sizeof(buffer), 0) == 11);
                        REQUIRE(strcmp(buffer, "hello world") == 0);

                        result = co_await event.on(asyncio::ev::What::READ);

                        REQUIRE(result);
                        REQUIRE(result.value() & asyncio::ev::What::READ);
                        REQUIRE(recv(fds[0], buffer, sizeof(buffer), 0) == 0);

                        evutil_closesocket(fds[0]);
                    }(),
                    [&]() -> zero::async::coroutine::Task<void> {
                        auto result = co_await asyncio::ev::Event(fds[1]).on(asyncio::ev::What::WRITE);

                        REQUIRE(result);
                        REQUIRE(result.value() & asyncio::ev::What::WRITE);

                        REQUIRE(send(fds[1], "hello world", 11, 0) == 11);

                        co_await asyncio::sleep(50ms);
                        evutil_closesocket(fds[1]);
                    }()
            );
        });
    }

    SECTION("wait timeout") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto result = co_await asyncio::ev::Event(fds[0]).on(asyncio::ev::What::READ, 50ms);

            REQUIRE(result);
            REQUIRE(result.value() & asyncio::ev::What::TIMEOUT);
        });
    }
}
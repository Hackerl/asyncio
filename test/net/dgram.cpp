#include <asyncio/net/dgram.h>
#include <asyncio/error.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("datagram network connection", "[dgram]") {
    std::array<std::byte, 2> message{std::byte{1}, std::byte{2}};

    SECTION("normal") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto server = asyncio::net::dgram::bind("127.0.0.1", 30000);
            REQUIRE(server);

            auto client = asyncio::net::dgram::bind("127.0.0.1", 30001);
            REQUIRE(client);

            co_await zero::async::coroutine::allSettled(
                    [&]() -> zero::async::coroutine::Task<void> {
                        std::byte data[1024];
                        auto result = co_await server->readFrom(data);
                        REQUIRE(result);

                        auto &[n, from] = *result;

                        REQUIRE(n);
                        REQUIRE(from.index() == 0);
                        REQUIRE(asyncio::net::stringify(from) == "127.0.0.1:30001");
                        REQUIRE(std::equal(data, data + n, message.begin()));

                        co_await server->writeTo({data, n}, from);
                        server->close();
                    }(),
                    [&]() -> zero::async::coroutine::Task<void> {
                        co_await client->writeTo(message, *asyncio::net::IPv4Address::from("127.0.0.1", 30000));

                        std::byte data[1024];
                        auto result = co_await client->readFrom(data);
                        REQUIRE(result);

                        auto &[n, from] = *result;

                        REQUIRE(n);
                        REQUIRE(from.index() == 0);
                        REQUIRE(asyncio::net::stringify(from) == "127.0.0.1:30000");
                        REQUIRE(std::equal(data, data + n, message.begin()));

                        client->close();
                    }()
            );
        });
    }

    SECTION("connect") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto server = asyncio::net::dgram::bind("127.0.0.1", 30000);
            REQUIRE(server);

            co_await zero::async::coroutine::allSettled(
                    [&]() -> zero::async::coroutine::Task<void> {
                        std::byte data[1024];
                        auto result = co_await server->readFrom(data);
                        REQUIRE(result);

                        auto &[n, from] = *result;

                        REQUIRE(n);
                        REQUIRE(from.index() == 0);
                        REQUIRE(asyncio::net::stringify(from).starts_with("127.0.0.1"));
                        REQUIRE(std::equal(data, data + n, message.begin()));

                        co_await server->writeTo({data, n}, from);
                        server->close();
                    }(),
                    [&]() -> zero::async::coroutine::Task<void> {
                        auto client = co_await asyncio::net::dgram::connect("127.0.0.1", 30000);
                        REQUIRE(client);

                        co_await client.value()->writeTo(message, *asyncio::net::IPv4Address::from("127.0.0.1", 30000));

                        std::byte data[1024];
                        auto result = co_await client.value()->readFrom(data);
                        REQUIRE(result);

                        auto &[n, from] = *result;

                        REQUIRE(n);
                        REQUIRE(from.index() == 0);
                        REQUIRE(asyncio::net::stringify(from) == "127.0.0.1:30000");
                        REQUIRE(std::equal(data, data + n, message.begin()));

                        client.value()->close();
                    }()
            );
        });
    }

    SECTION("read timeout") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto socket = asyncio::net::dgram::bind("127.0.0.1", 30000);
            REQUIRE(socket);

            socket->setTimeout(50ms, 0ms);

            std::byte data[1024];
            auto result = co_await socket->readFrom(data);

            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);

            socket->close();
        });
    }

    SECTION("close") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto socket = asyncio::net::dgram::bind("127.0.0.1", 30000);
            REQUIRE(socket);

            co_await zero::async::coroutine::allSettled(
                    [&]() -> zero::async::coroutine::Task<void> {
                        std::byte data[1024];
                        auto result = co_await socket->readFrom(data);

                        REQUIRE(!result);
                        REQUIRE(result.error() == asyncio::Error::IO_EOF);
                    }(),
                    [&]() -> zero::async::coroutine::Task<void> {
                        co_await asyncio::sleep(50ms);
                        socket->close();
                    }()
            );
        });
    }

    SECTION("cancel") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto socket = asyncio::net::dgram::bind("127.0.0.1", 30000);
            REQUIRE(socket);

            std::byte data[1024];
            auto task = socket->readFrom(data);
            auto result = co_await asyncio::timeout(task, 50ms);

            REQUIRE(task.done());
            REQUIRE(task.result().error() == std::errc::operation_canceled);
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);

            socket->close();
        });
    }
}
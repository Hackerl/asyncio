#include <asyncio/net/dgram.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <fmt/std.h>

using namespace std::chrono_literals;

TEST_CASE("datagram network connection", "[dgram]") {
    SECTION("normal") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto server = asyncio::net::dgram::bind("127.0.0.1", 30000);
            REQUIRE(server);

            auto client = asyncio::net::dgram::bind("127.0.0.1", 30001);
            REQUIRE(client);

            co_await zero::async::coroutine::allSettled(
                    [](auto server) -> zero::async::coroutine::Task<void> {
                        std::byte data[1024];
                        auto result = co_await server.readFrom(data);
                        REQUIRE(result);

                        auto &[n, from] = *result;

                        REQUIRE(n);
                        REQUIRE(from.index() == 0);
                        REQUIRE(fmt::to_string(from) == "variant(127.0.0.1:30001)");
                        REQUIRE(data[0] == std::byte{1});
                        REQUIRE(data[1] == std::byte{2});

                        co_await server.writeTo({data, n}, from);
                    }(std::move(*server)),
                    [](auto client) -> zero::async::coroutine::Task<void> {
                        auto message = {std::byte{1}, std::byte{2}};
                        co_await client.writeTo(message, *asyncio::net::IPv4Address::from("127.0.0.1", 30000));

                        std::byte data[1024];
                        auto result = co_await client.readFrom(data);
                        REQUIRE(result);

                        auto &[n, from] = *result;

                        REQUIRE(n);
                        REQUIRE(from.index() == 0);
                        REQUIRE(fmt::to_string(from) == "variant(127.0.0.1:30000)");
                        REQUIRE(std::equal(data, data + n, message.begin()));
                    }(std::move(*client))
            );
        });
    }

    SECTION("connect") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto server = asyncio::net::dgram::bind("127.0.0.1", 30000);
            REQUIRE(server);

            co_await zero::async::coroutine::allSettled(
                    [](auto server) -> zero::async::coroutine::Task<void> {
                        std::byte data[1024];
                        auto result = co_await server.readFrom(data);
                        REQUIRE(result);

                        auto &[n, from] = *result;

                        REQUIRE(n);
                        REQUIRE(from.index() == 0);
                        REQUIRE(fmt::to_string(from).find("127.0.0.1") != std::string::npos);
                        REQUIRE(data[0] == std::byte{1});
                        REQUIRE(data[1] == std::byte{2});

                        co_await server.writeTo({data, n}, from);
                    }(std::move(*server)),
                    []() -> zero::async::coroutine::Task<void> {
                        auto client = std::move(co_await asyncio::net::dgram::connect("127.0.0.1", 30000));
                        REQUIRE(client);

                        auto message = {std::byte{1}, std::byte{2}};
                        auto result = co_await client->write(message);
                        REQUIRE(result);
                        REQUIRE(*result == message.size());

                        std::byte data[1024];
                        auto res = co_await client->readFrom(data);
                        REQUIRE(res);

                        auto &[n, from] = *res;

                        REQUIRE(n);
                        REQUIRE(from.index() == 0);
                        REQUIRE(fmt::to_string(from) == "variant(127.0.0.1:30000)");
                        REQUIRE(std::equal(data, data + n, message.begin()));
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
        });
    }

    SECTION("close") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto socket = asyncio::net::dgram::bind("127.0.0.1", 30000)
                    .transform([](asyncio::net::dgram::Socket &&socket) {
                        return std::make_shared<asyncio::net::dgram::Socket>(std::move(socket));
                    });

            REQUIRE(socket);

            co_await zero::async::coroutine::allSettled(
                    [](auto socket) -> zero::async::coroutine::Task<void> {
                        std::byte data[1024];
                        auto result = co_await socket->readFrom(data);

                        REQUIRE(!result);
                        REQUIRE(result.error() == asyncio::Error::IO_EOF);
                    }(*socket),
                    [](auto socket) -> zero::async::coroutine::Task<void> {
                        co_await asyncio::sleep(50ms);
                        socket->close();
                    }(*socket)
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
        });
    }
}
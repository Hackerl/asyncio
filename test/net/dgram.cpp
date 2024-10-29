#include <catch_extensions.h>
#include <asyncio/net/dgram.h>
#include <catch2/catch_test_macros.hpp>

constexpr std::string_view MESSAGE = "hello world";

ASYNC_TEST_CASE("UDP socket", "[net]") {
    auto socket = asyncio::net::UDPSocket::bind("127.0.0.1", 0);
    REQUIRE(socket);

    SECTION("fd") {
        const auto fd = socket->fd();
#ifdef _WIN32
        REQUIRE(fd != nullptr);
#else
        REQUIRE(fd >= 0);
#endif
    }

    SECTION("local address") {
        const auto address = socket->localAddress();
        REQUIRE(address);
        REQUIRE(std::get<asyncio::net::IPv4Address>(*address).ip == asyncio::net::LOCALHOST_IPV4);
    }

    SECTION("remote address") {
        REQUIRE_ERROR(socket->remoteAddress(), std::errc::not_connected);
    }

    SECTION("read") {
        auto peer = asyncio::net::UDPSocket::bind("127.0.0.1", 0);
        REQUIRE(peer);

        const auto destination = socket->localAddress();
        REQUIRE(destination);

        REQUIRE(co_await peer->writeTo(std::as_bytes(std::span{MESSAGE}), *destination) == MESSAGE.size());

        std::string message;
        message.resize(MESSAGE.size());

        REQUIRE(co_await socket->read(std::as_writable_bytes(std::span{message})) == MESSAGE.size());
        REQUIRE(message == MESSAGE);
    }

    SECTION("write") {
        REQUIRE_ERROR(
            co_await socket->write(std::as_bytes(std::span{MESSAGE})),
            std::errc::destination_address_required
        );
    }

    SECTION("read from") {
        auto peer = asyncio::net::UDPSocket::bind("127.0.0.1", 0);
        REQUIRE(peer);

        const auto destination = socket->localAddress();
        REQUIRE(destination);

        REQUIRE(co_await peer->writeTo(std::as_bytes(std::span{MESSAGE}), *destination) == MESSAGE.size());

        std::string message;
        message.resize(MESSAGE.size());

        const auto result = co_await socket->readFrom(std::as_writable_bytes(std::span{message}));
        REQUIRE(result);
        REQUIRE(result->first == MESSAGE.size());
        REQUIRE(message == MESSAGE);

        const auto address = peer->localAddress();
        REQUIRE(address);
        REQUIRE(result->second == *address);
    }

    SECTION("write to") {
        auto peer = asyncio::net::UDPSocket::bind("127.0.0.1", 0);
        REQUIRE(peer);

        const auto destination = peer->localAddress();
        REQUIRE(destination);

        REQUIRE(co_await socket->writeTo(std::as_bytes(std::span{MESSAGE}), *destination) == MESSAGE.size());

        std::string message;
        message.resize(MESSAGE.size());

        const auto result = co_await peer->readFrom(std::as_writable_bytes(std::span{message}));
        REQUIRE(result);
        REQUIRE(result->first == MESSAGE.size());
        REQUIRE(message == MESSAGE);

        const auto address = socket->localAddress();
        REQUIRE(address);
        REQUIRE(result->second == *address);
    }

    SECTION("close") {
        REQUIRE(co_await socket->close());
    }
}

ASYNC_TEST_CASE("UDP socket connect", "[net]") {
    auto peer = asyncio::net::UDPSocket::bind("127.0.0.1", 0);
    REQUIRE(peer);

    const auto peerAddress = peer->localAddress();
    REQUIRE(peerAddress);

    auto socket = asyncio::net::UDPSocket::connect(std::get<asyncio::net::IPv4Address>(*peerAddress));
    REQUIRE(socket);

    SECTION("remote address") {
        REQUIRE(socket->remoteAddress() == peerAddress);
    }

    SECTION("write") {
        REQUIRE(co_await socket->write(std::as_bytes(std::span{MESSAGE})) == MESSAGE.size());

        std::string message;
        message.resize(MESSAGE.size());

        const auto result = co_await peer->readFrom(std::as_writable_bytes(std::span{message}));
        REQUIRE(result);
        REQUIRE(result->first == MESSAGE.size());
        REQUIRE(message == MESSAGE);

        const auto address = socket->localAddress();
        REQUIRE(address);
        REQUIRE(result->second == *address);
    }
}

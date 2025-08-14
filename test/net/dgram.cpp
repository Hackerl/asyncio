#include <catch_extensions.h>
#include <asyncio/net/dgram.h>

ASYNC_TEST_CASE("UDP socket", "[net::dgram]") {
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

    const auto input = GENERATE(take(1, randomBytes(1, 1024)));

    SECTION("read") {
        auto peer = asyncio::net::UDPSocket::bind("127.0.0.1", 0);
        REQUIRE(peer);

        const auto destination = socket->localAddress();
        REQUIRE(destination);

        REQUIRE(co_await peer->writeTo(input, *destination) == input.size());

        std::vector<std::byte> data;
        data.resize(input.size());

        REQUIRE(co_await socket->read(data) == input.size());
        REQUIRE(data == input);
    }

    SECTION("write") {
        REQUIRE_ERROR(co_await socket->write(input), std::errc::destination_address_required);
    }

    SECTION("read from") {
        auto peer = asyncio::net::UDPSocket::bind("127.0.0.1", 0);
        REQUIRE(peer);

        const auto destination = socket->localAddress();
        REQUIRE(destination);

        REQUIRE(co_await peer->writeTo(input, *destination) == input.size());

        std::vector<std::byte> data;
        data.resize(input.size());

        const auto result = co_await socket->readFrom(data);
        REQUIRE(result);
        REQUIRE(result->first == input.size());
        REQUIRE(data == input);

        const auto address = peer->localAddress();
        REQUIRE(address);
        REQUIRE(result->second == *address);
    }

    SECTION("write to") {
        auto peer = asyncio::net::UDPSocket::bind("127.0.0.1", 0);
        REQUIRE(peer);

        const auto destination = peer->localAddress();
        REQUIRE(destination);

        REQUIRE(co_await socket->writeTo(input, *destination) == input.size());

        std::vector<std::byte> data;
        data.resize(input.size());

        const auto result = co_await peer->readFrom(data);
        REQUIRE(result);
        REQUIRE(result->first == input.size());
        REQUIRE(data == input);

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
        const auto input = GENERATE(take(1, randomBytes(1, 1024)));

        REQUIRE(co_await socket->write(input) == input.size());

        std::vector<std::byte> data;
        data.resize(input.size());

        const auto result = co_await peer->readFrom(data);
        REQUIRE(result);
        REQUIRE(result->first == input.size());
        REQUIRE(data == input);

        const auto address = socket->localAddress();
        REQUIRE(address);
        REQUIRE(result->second == *address);
    }
}

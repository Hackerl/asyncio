#include <catch_extensions.h>
#include <asyncio/net/dgram.h>
#include <asyncio/error.h>

ASYNC_TEST_CASE("UDP socket", "[net::dgram]") {
    auto socket = co_await asyncio::error::guard(asyncio::net::UDPSocket::bind("127.0.0.1", 0));

    SECTION("fd") {
        const auto fd = socket.fd();
#ifdef _WIN32
        REQUIRE(fd != nullptr);
#else
        REQUIRE(fd >= 0);
#endif
    }

    SECTION("local address") {
        const auto address = socket.localAddress();
        REQUIRE(address);
        REQUIRE(std::get<asyncio::net::IPv4Address>(*address).ip == asyncio::net::LocalhostIPv4);
    }

    SECTION("remote address") {
        REQUIRE_ERROR(socket.remoteAddress(), std::errc::not_connected);
    }

    const auto input = GENERATE(take(1, randomBytes(1, 1024)));

    SECTION("read") {
        auto peer = co_await asyncio::error::guard(asyncio::net::UDPSocket::bind("127.0.0.1", 0));

        if (const auto destination = co_await asyncio::error::guard(socket.localAddress());
            co_await asyncio::error::guard(peer.writeTo(input, destination)) != input.size())
            throw co_await asyncio::error::StacktraceError<std::runtime_error>::make("Failed to send data");

        std::vector<std::byte> data;
        data.resize(input.size());

        REQUIRE(co_await socket.read(data) == input.size());
        REQUIRE(data == input);
    }

    SECTION("write") {
        REQUIRE_ERROR(co_await socket.write(input), std::errc::destination_address_required);
    }

    SECTION("read from") {
        auto peer = co_await asyncio::error::guard(asyncio::net::UDPSocket::bind("127.0.0.1", 0));

        if (const auto destination = co_await asyncio::error::guard(socket.localAddress());
            co_await asyncio::error::guard(peer.writeTo(input, destination)) != input.size())
            throw co_await asyncio::error::StacktraceError<std::runtime_error>::make("Failed to send data");

        std::vector<std::byte> data;
        data.resize(input.size());

        const auto result = co_await socket.readFrom(data);
        REQUIRE(result);
        REQUIRE(result->first == input.size());
        REQUIRE(result->second == co_await asyncio::error::guard(peer.localAddress()));
        REQUIRE(data == input);
    }

    SECTION("write to") {
        auto peer = co_await asyncio::error::guard(asyncio::net::UDPSocket::bind("127.0.0.1", 0));
        const auto destination = co_await asyncio::error::guard(peer.localAddress());

        REQUIRE(co_await socket.writeTo(input, destination) == input.size());

        std::vector<std::byte> data;
        data.resize(input.size());

        const auto [n, address] = co_await asyncio::error::guard(peer.readFrom(data));
        REQUIRE(n == input.size());
        REQUIRE(address == co_await asyncio::error::guard(socket.localAddress()));
        REQUIRE(data == input);
    }

    SECTION("close") {
        REQUIRE(co_await socket.close());
    }
}

ASYNC_TEST_CASE("connected UDP socket", "[net]") {
    auto peer = co_await asyncio::error::guard(asyncio::net::UDPSocket::bind("127.0.0.1", 0));
    const auto peerAddress = co_await asyncio::error::guard(peer.localAddress());

    auto socket = co_await asyncio::error::guard(
        asyncio::net::UDPSocket::connect(std::get<asyncio::net::IPv4Address>(peerAddress))
    );

    SECTION("remote address") {
        REQUIRE(socket.remoteAddress() == peerAddress);
    }

    SECTION("write") {
        const auto input = GENERATE(take(1, randomBytes(1, 1024)));

        REQUIRE(co_await socket.write(input) == input.size());

        std::vector<std::byte> data;
        data.resize(input.size());

        const auto [n, address] = co_await asyncio::error::guard(peer.readFrom(data));
        REQUIRE(n == input.size());
        REQUIRE(address == co_await asyncio::error::guard(socket.localAddress()));
        REQUIRE(data == input);
    }
}

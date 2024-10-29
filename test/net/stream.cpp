#include <catch_extensions.h>
#include <asyncio/net/stream.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

#ifndef _WIN32
#include <unistd.h>
#include <asyncio/fs.h>
#endif

constexpr std::string_view MESSAGE = "hello world";

ASYNC_TEST_CASE("TCP stream", "[net]") {
    auto listener = asyncio::net::TCPListener::listen("127.0.0.1", 0);
    REQUIRE(listener);

    const auto serverAddress = listener->address();
    REQUIRE(serverAddress);

    auto result = co_await all(
        listener->accept(),
        asyncio::net::TCPStream::connect(*serverAddress)
    );
    REQUIRE(result);

    auto &server = result->at(0);
    auto &client = result->at(1);

    SECTION("fd") {
        const auto fd = client.fd();
#ifdef _WIN32
        REQUIRE(fd != nullptr);
#else
        REQUIRE(fd >= 0);
#endif
    }

    SECTION("local address") {
        const auto address = server.localAddress();
        REQUIRE(address);
        REQUIRE(std::get<asyncio::net::IPv4Address>(*address) == std::get<asyncio::net::IPv4Address>(*serverAddress));
    }

    SECTION("remote address") {
        const auto address = client.remoteAddress();
        REQUIRE(address);
        REQUIRE(std::get<asyncio::net::IPv4Address>(*address) == std::get<asyncio::net::IPv4Address>(*serverAddress));
    }

    SECTION("read") {
        REQUIRE(co_await server.writeAll(std::as_bytes(std::span{MESSAGE})));

        std::string message;
        message.resize(MESSAGE.size());

        REQUIRE(co_await client.readExactly(std::as_writable_bytes(std::span{message})));
        REQUIRE(message == MESSAGE);
    }

    SECTION("write") {
        REQUIRE(co_await client.writeAll(std::as_bytes(std::span{MESSAGE})));

        std::string message;
        message.resize(MESSAGE.size());

        REQUIRE(co_await server.readExactly(std::as_writable_bytes(std::span{message})));
        REQUIRE(message == MESSAGE);
    }

    SECTION("shutdown") {
        REQUIRE(co_await client.shutdown());

        std::array<std::byte, 1024> data{};
        REQUIRE(co_await server.read(data) == 0);
    }

    SECTION("close reset") {
        REQUIRE(co_await client.closeReset());

        std::array<std::byte, 1024> data{};
        REQUIRE_ERROR(co_await server.read(data), std::errc::connection_reset);
    }

    SECTION("close") {
        REQUIRE(co_await client.close());

        std::array<std::byte, 1024> data{};
        REQUIRE(co_await server.read(data) == 0);
    }
}

#ifdef _WIN32
ASYNC_TEST_CASE("named pipe stream", "[net]") {
    constexpr auto name = R"(\\.\pipe\asyncio-net-named-pipe)";

    auto listener = asyncio::net::NamedPipeListener::listen(name);
    REQUIRE(listener);

    auto result = co_await all(
        listener->accept(),
        asyncio::net::NamedPipeStream::connect(name)
    );
    REQUIRE(result);

    auto &server = result->at(0);
    auto &client = result->at(1);

    SECTION("fd") {
        const auto fd = client.fd();
#ifdef _WIN32
        REQUIRE(fd != nullptr);
#else
        REQUIRE(fd >= 0);
#endif
    }

    SECTION("client process id") {
        REQUIRE(server.clientProcessID() == GetCurrentProcessId());
    }

    SECTION("server process id") {
        REQUIRE(client.clientProcessID() == GetCurrentProcessId());
    }

    SECTION("read") {
        REQUIRE(co_await server.writeAll(std::as_bytes(std::span{MESSAGE})));

        std::string message;
        message.resize(MESSAGE.size());

        REQUIRE(co_await client.readExactly(std::as_writable_bytes(std::span{message})));
        REQUIRE(message == MESSAGE);
    }

    SECTION("write") {
        REQUIRE(co_await client.writeAll(std::as_bytes(std::span{MESSAGE})));

        std::string message;
        message.resize(MESSAGE.size());

        REQUIRE(co_await server.readExactly(std::as_writable_bytes(std::span{message})));
        REQUIRE(message == MESSAGE);
    }

    SECTION("close") {
        REQUIRE(co_await client.close());

        std::array<std::byte, 1024> data{};
        REQUIRE(co_await server.read(data) == 0);
    }
}
#endif

#if defined(__unix__) || defined(__APPLE__)
ASYNC_TEST_CASE("UNIX domain stream", "[net]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-net-unix.sock";

    auto listener = asyncio::net::UnixListener::listen(path.string());
    REQUIRE(listener);

    auto result = co_await all(
        listener->accept(),
        asyncio::net::UnixStream::connect(path.string())
    );
    REQUIRE(result);

    auto &server = result->at(0);
    auto &client = result->at(1);

    SECTION("fd") {
        const auto fd = client.fd();
#ifdef _WIN32
        REQUIRE(fd != nullptr);
#else
        REQUIRE(fd >= 0);
#endif
    }

    SECTION("local address") {
        const auto address = server.localAddress();
        REQUIRE(address);
        REQUIRE(std::holds_alternative<asyncio::net::UnixAddress>(*address));
        REQUIRE(std::get<asyncio::net::UnixAddress>(*address).path == path.string());
    }

    SECTION("remote address") {
        const auto address = client.remoteAddress();
        REQUIRE(address);
        REQUIRE(std::holds_alternative<asyncio::net::UnixAddress>(*address));
        REQUIRE(std::get<asyncio::net::UnixAddress>(*address).path == path.string());
    }

    SECTION("peer credential") {
        const auto credential = client.peerCredential();
        REQUIRE(credential);
        REQUIRE(credential->uid == getuid());
        REQUIRE(credential->gid == getgid());
        REQUIRE(*credential->pid == getpid());
    }

    SECTION("read") {
        REQUIRE(co_await server.writeAll(std::as_bytes(std::span{MESSAGE})));

        std::string message;
        message.resize(MESSAGE.size());

        REQUIRE(co_await client.readExactly(std::as_writable_bytes(std::span{message})));
        REQUIRE(message == MESSAGE);
    }

    SECTION("write") {
        REQUIRE(co_await client.writeAll(std::as_bytes(std::span{MESSAGE})));

        std::string message;
        message.resize(MESSAGE.size());

        REQUIRE(co_await server.readExactly(std::as_writable_bytes(std::span{message})));
        REQUIRE(message == MESSAGE);
    }

    SECTION("close") {
        REQUIRE(co_await client.close());

        std::array<std::byte, 1024> data{};
        REQUIRE(co_await server.read(data) == 0);
    }
}
#endif

#if defined(__linux__)
ASYNC_TEST_CASE("abstract UNIX domain stream", "[net]") {
    constexpr auto name = "@asyncio-net-abstract-unix.sock.sock";

    auto listener = asyncio::net::UnixListener::listen(name);
    REQUIRE(listener);

    auto result = co_await all(
        listener->accept(),
        asyncio::net::UnixStream::connect(name)
    );
    REQUIRE(result);

    auto &server = result->at(0);
    auto &client = result->at(1);

    SECTION("fd") {
        const auto fd = client.fd();
#ifdef _WIN32
        REQUIRE(fd != nullptr);
#else
        REQUIRE(fd >= 0);
#endif
    }

#if UV_VERSION_MAJOR == 1 && UV_VERSION_MINOR >= 48
    SECTION("local address") {
        const auto address = server.localAddress();
        REQUIRE(address);
        REQUIRE(std::holds_alternative<asyncio::net::UnixAddress>(*address));
        REQUIRE(std::get<asyncio::net::UnixAddress>(*address).path == name);
    }

    SECTION("remote address") {
        const auto address = client.remoteAddress();
        REQUIRE(address);
        REQUIRE(std::holds_alternative<asyncio::net::UnixAddress>(*address));
        REQUIRE(std::get<asyncio::net::UnixAddress>(*address).path == name);
    }
#endif

    SECTION("peer credential") {
        const auto credential = client.peerCredential();
        REQUIRE(credential);
        REQUIRE(credential->uid == getuid());
        REQUIRE(credential->gid == getgid());
        REQUIRE(*credential->pid == getpid());
    }

    SECTION("read") {
        REQUIRE(co_await server.writeAll(std::as_bytes(std::span{MESSAGE})));

        std::string message;
        message.resize(MESSAGE.size());

        REQUIRE(co_await client.readExactly(std::as_writable_bytes(std::span{message})));
        REQUIRE(message == MESSAGE);
    }

    SECTION("write") {
        REQUIRE(co_await client.writeAll(std::as_bytes(std::span{MESSAGE})));

        std::string message;
        message.resize(MESSAGE.size());

        REQUIRE(co_await server.readExactly(std::as_writable_bytes(std::span{message})));
        REQUIRE(message == MESSAGE);
    }

    SECTION("close") {
        REQUIRE(co_await client.close());

        std::array<std::byte, 1024> data{};
        REQUIRE(co_await server.read(data) == 0);
    }
}
#endif
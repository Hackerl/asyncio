#include <catch_extensions.h>
#include <asyncio/net/stream.h>
#include <asyncio/error.h>
#include <catch2/matchers/catch_matchers_all.hpp>

#ifndef _WIN32
#include <unistd.h>
#include <asyncio/fs.h>
#endif

ASYNC_TEST_CASE("TCP stream", "[net::tcp]") {
    auto listener = co_await asyncio::error::guard(asyncio::net::TCPListener::listen("127.0.0.1", 0));
    const auto serverAddress = co_await asyncio::error::guard(listener.address());

    auto [server, client] = co_await asyncio::error::guard(
        all(
            listener.accept(),
            asyncio::net::TCPStream::connect(serverAddress)
        )
    );

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
        REQUIRE(std::get<asyncio::net::IPv4Address>(*address) == std::get<asyncio::net::IPv4Address>(serverAddress));
    }

    SECTION("remote address") {
        const auto address = client.remoteAddress();
        REQUIRE(address);
        REQUIRE(std::get<asyncio::net::IPv4Address>(*address) == std::get<asyncio::net::IPv4Address>(serverAddress));
    }

    const auto input = GENERATE(take(1, randomBytes(1, 102400)));

    SECTION("read") {
        auto task = server.writeAll(input);

        std::vector<std::byte> data;
        data.resize(input.size());

        REQUIRE(co_await client.readExactly(data));
        co_await asyncio::error::guard(std::move(task));

        REQUIRE(data == input);
    }

    SECTION("write") {
        std::vector<std::byte> data;
        data.resize(input.size());

        auto task = server.readExactly(data);

        REQUIRE(co_await client.writeAll(input));
        co_await asyncio::error::guard(std::move(task));

        REQUIRE(data == input);
    }

    SECTION("shutdown") {
        {
            REQUIRE(co_await client.shutdown());

            std::array<std::byte, 1024> data{};
            REQUIRE(co_await server.read(data) == 0);
        }

        {
            auto task = server.writeAll(input);

            std::vector<std::byte> data;
            data.resize(input.size());

            REQUIRE(co_await client.readExactly(data));
            REQUIRE(co_await task);
            REQUIRE(data == input);
        }
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
    const auto name = fmt::format(R"(\\.\pipe\asyncio-{})", GENERATE(take(1, randomAlphanumericString(8, 16))));

    auto listener = co_await asyncio::error::guard(asyncio::net::NamedPipeListener::listen(name));

    auto [server, client] = co_await asyncio::error::guard(
        all(
            listener.accept(),
            asyncio::net::NamedPipeStream::connect(name)
        )
    );

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

    const auto input = GENERATE(take(1, randomBytes(1, 102400)));

    SECTION("read") {
        auto task = server.writeAll(input);

        std::vector<std::byte> data;
        data.resize(input.size());

        REQUIRE(co_await client.readExactly(data));
        co_await asyncio::error::guard(std::move(task));

        REQUIRE(data == input);
    }

    SECTION("write") {
        std::vector<std::byte> data;
        data.resize(input.size());

        auto task = server.readExactly(data);

        REQUIRE(co_await client.writeAll(input));
        co_await asyncio::error::guard(std::move(task));

        REQUIRE(data == input);
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
    const auto temp = co_await asyncio::error::guard(asyncio::fs::temporaryDirectory());
    const auto path = temp / GENERATE(take(1, randomAlphanumericString(8, 16)));

    auto listener = co_await asyncio::error::guard(asyncio::net::UnixListener::listen(path.string()));

    auto [server, client] = co_await asyncio::error::guard(
        all(
            listener.accept(),
            asyncio::net::UnixStream::connect(path.string())
        )
    );

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
        REQUIRE(std::get<asyncio::net::UnixAddress>(*address).path == path);
    }

    SECTION("remote address") {
        const auto address = client.remoteAddress();
        REQUIRE(address);
        REQUIRE(std::get<asyncio::net::UnixAddress>(*address).path == path);
    }

    SECTION("peer credential") {
        const auto credential = client.peerCredential();
        REQUIRE(credential);
        REQUIRE(credential->uid == getuid());
        REQUIRE(credential->gid == getgid());
        REQUIRE(credential->pid);
        REQUIRE(*credential->pid == getpid());
    }

    const auto input = GENERATE(take(1, randomBytes(1, 102400)));

    SECTION("read") {
        auto task = server.writeAll(input);

        std::vector<std::byte> data;
        data.resize(input.size());

        REQUIRE(co_await client.readExactly(data));
        co_await asyncio::error::guard(std::move(task));

        REQUIRE(data == input);
    }

    SECTION("write") {
        std::vector<std::byte> data;
        data.resize(input.size());

        auto task = server.readExactly(data);

        REQUIRE(co_await client.writeAll(input));
        co_await asyncio::error::guard(std::move(task));

        REQUIRE(data == input);
    }

    SECTION("close") {
        REQUIRE(co_await client.close());

        std::array<std::byte, 1024> data{};
        REQUIRE(co_await server.read(data) == 0);
    }
}
#endif

#ifdef __linux__
ASYNC_TEST_CASE("abstract UNIX domain stream", "[net]") {
    const auto name = fmt::format("@asyncio-{}", GENERATE(take(1, randomAlphanumericString(8, 16))));

    auto listener = co_await asyncio::error::guard(asyncio::net::UnixListener::listen(name));

    auto [server, client] = co_await asyncio::error::guard(
        all(
            listener.accept(),
            asyncio::net::UnixStream::connect(name)
        )
    );

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
        REQUIRE(std::get<asyncio::net::UnixAddress>(*address).path == name);
    }

    SECTION("remote address") {
        const auto address = client.remoteAddress();
        REQUIRE(address);
        REQUIRE(std::get<asyncio::net::UnixAddress>(*address).path == name);
    }
#endif

    SECTION("peer credential") {
        const auto credential = client.peerCredential();
        REQUIRE(credential);
        REQUIRE(credential->uid == getuid());
        REQUIRE(credential->gid == getgid());
        REQUIRE(credential->pid);
        REQUIRE(*credential->pid == getpid());
    }

    const auto input = GENERATE(take(1, randomBytes(1, 102400)));

    SECTION("read") {
        auto task = server.writeAll(input);

        std::vector<std::byte> data;
        data.resize(input.size());

        REQUIRE(co_await client.readExactly(data));
        co_await asyncio::error::guard(std::move(task));

        REQUIRE(data == input);
    }

    SECTION("write") {
        std::vector<std::byte> data;
        data.resize(input.size());

        auto task = server.readExactly(data);

        REQUIRE(co_await client.writeAll(input));
        co_await asyncio::error::guard(std::move(task));

        REQUIRE(data == input);
    }

    SECTION("close") {
        REQUIRE(co_await client.close());

        std::array<std::byte, 1024> data{};
        REQUIRE(co_await server.read(data) == 0);
    }
}
#endif

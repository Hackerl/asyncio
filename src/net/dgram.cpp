#include <asyncio/net/dgram.h>
#include <asyncio/net/dns.h>
#include <zero/expect.h>
#include <cassert>

constexpr auto READ_INDEX = 0;
constexpr auto WRITE_INDEX = 1;

asyncio::net::dgram::Socket::Socket(const FileDescriptor fd, std::array<ev::Event, 2> events)
    : mFD(fd), mEvents(std::move(events)) {
}

asyncio::net::dgram::Socket::Socket(Socket &&rhs) noexcept
    : mFD(std::exchange(rhs.mFD, INVALID_FILE_DESCRIPTOR)), mEvents(std::move(rhs.mEvents)) {
    assert(!mEvents[READ_INDEX].pending());
    assert(!mEvents[WRITE_INDEX].pending());
}

asyncio::net::dgram::Socket &asyncio::net::dgram::Socket::operator=(Socket &&rhs) noexcept {
    assert(!rhs.mEvents[READ_INDEX].pending());
    assert(!rhs.mEvents[WRITE_INDEX].pending());

    mFD = std::exchange(rhs.mFD, INVALID_FILE_DESCRIPTOR);
    mEvents = std::move(rhs.mEvents);

    return *this;
}

asyncio::net::dgram::Socket::~Socket() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        return;

    evutil_closesocket(mFD);
}

tl::expected<asyncio::net::dgram::Socket, std::error_code> asyncio::net::dgram::Socket::make(const int family) {
#ifdef _WIN32
    const auto fd = static_cast<FileDescriptor>(socket(family, SOCK_DGRAM, 0));
#else
    const auto fd = socket(family, SOCK_DGRAM, 0);
#endif

    if (fd == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    if (evutil_make_socket_nonblocking(fd) == -1) {
        evutil_closesocket(fd);
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());
    }

    std::array events = {
        ev::Event::make(fd, ev::What::READ),
        ev::Event::make(fd, ev::What::WRITE)
    };

    if (!events[0]) {
        evutil_closesocket(fd);
        return tl::unexpected(events[0].error());
    }

    if (!events[1]) {
        evutil_closesocket(fd);
        return tl::unexpected(events[1].error());
    }

    return Socket{fd, {*std::move(events[0]), *std::move(events[1])}};
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::net::dgram::Socket::read(const std::span<std::byte> data) {
    assert(!data.empty());

    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(IOError::BAD_FILE_DESCRIPTOR);

    if (mEvents[READ_INDEX].pending())
        co_return tl::unexpected(IOError::DEVICE_OR_RESOURCE_BUSY);

    tl::expected<std::size_t, std::error_code> result;

    while (true) {
#ifdef _WIN32
        const int n = recv(mFD, reinterpret_cast<char *>(data.data()), static_cast<int>(data.size()), 0);

        if (n == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            co_return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());
#else
        const ssize_t n = recv(mFD, data.data(), data.size(), 0);

        if (n == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());
            break;
        }
#endif
        if (n >= 0) {
            *result = n;
            break;
        }

        const auto what = co_await mEvents[READ_INDEX].on();

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }
    }

    co_return result;
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::net::dgram::Socket::write(const std::span<const std::byte> data) {
    assert(!data.empty());

    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(IOError::BAD_FILE_DESCRIPTOR);

    if (mEvents[WRITE_INDEX].pending())
        co_return tl::unexpected(IOError::DEVICE_OR_RESOURCE_BUSY);

    tl::expected<std::size_t, std::error_code> result;

    while (true) {
#ifdef _WIN32
        const int n = send(mFD, reinterpret_cast<const char *>(data.data()), static_cast<int>(data.size()), 0);

        if (n == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            co_return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());
#else
        const ssize_t n = send(mFD, data.data(), data.size(), 0);

        if (n == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());
            break;
        }
#endif
        assert(n != 0);

        if (n > 0) {
            *result = n;
            break;
        }

        const auto what = co_await mEvents[WRITE_INDEX].on();

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }
    }

    co_return result;
}

zero::async::coroutine::Task<void, std::error_code> asyncio::net::dgram::Socket::close() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(IOError::BAD_FILE_DESCRIPTOR);

    assert(!mEvents[READ_INDEX].pending());
    assert(!mEvents[WRITE_INDEX].pending());

    if (evutil_closesocket(std::exchange(mFD, INVALID_FILE_DESCRIPTOR)) != 0)
        co_return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    co_return {};
}

zero::async::coroutine::Task<std::pair<std::size_t, asyncio::net::Address>, std::error_code>
asyncio::net::dgram::Socket::readFrom(const std::span<std::byte> data) {
    assert(!data.empty());

    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(IOError::BAD_FILE_DESCRIPTOR);

    if (mEvents[READ_INDEX].pending())
        co_return tl::unexpected(IOError::DEVICE_OR_RESOURCE_BUSY);

    tl::expected<std::pair<std::size_t, Address>, std::error_code> result;

    while (true) {
        sockaddr_storage storage = {};
        socklen_t length = sizeof(sockaddr_storage);

#ifdef _WIN32
        const int n = recvfrom(
            mFD,
            reinterpret_cast<char *>(data.data()),
            static_cast<int>(data.size()),
            0,
            reinterpret_cast<sockaddr *>(&storage),
            &length
        );

        if (n == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            co_return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());
#else
        const ssize_t n = recvfrom(mFD, data.data(), data.size(), 0, reinterpret_cast<sockaddr *>(&storage), &length);

        if (n == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());
            break;
        }
#endif
        if (n == 0)
            break;

        if (n > 0) {
            const auto address = addressFrom(reinterpret_cast<sockaddr *>(&storage), length);

            if (!address) {
                result = tl::unexpected(address.error());
                break;
            }

            result = {n, *address};
            break;
        }

        const auto what = co_await mEvents[READ_INDEX].on();

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }
    }

    co_return result;
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::net::dgram::Socket::writeTo(const std::span<const std::byte> data, const Address address) {
    assert(!data.empty());

    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(IOError::BAD_FILE_DESCRIPTOR);

    if (mEvents[WRITE_INDEX].pending())
        co_return tl::unexpected(IOError::DEVICE_OR_RESOURCE_BUSY);

    const auto socketAddress = socketAddressFrom(address);
    CO_EXPECT(socketAddress);

    tl::expected<std::size_t, std::error_code> result;

    while (true) {
#ifdef _WIN32
        const int n = sendto(
            mFD,
            reinterpret_cast<const char *>(data.data()),
            static_cast<int>(data.size()),
            0,
            socketAddress->first.get(),
#ifdef _WIN32
            socketAddress->second
#else
            static_cast<int>(socketAddress->second)
#endif
        );

        if (n == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            co_return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());
#else
        const ssize_t n = sendto(
            mFD,
            data.data(),
            data.size(),
            0,
            socketAddress->first.get(),
            socketAddress->second
        );

        if (n == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());
            break;
        }
#endif
        assert(n != 0);

        if (n > 0) {
            *result = n;
            break;
        }

        const auto what = co_await mEvents[WRITE_INDEX].on();

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }
    }

    co_return result;
}

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::dgram::Socket::localAddress() const {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected(IOError::BAD_FILE_DESCRIPTOR);

    return addressFrom(mFD, false);
}

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::dgram::Socket::remoteAddress() const {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected(IOError::BAD_FILE_DESCRIPTOR);

    return addressFrom(mFD, true);
}


tl::expected<void, std::error_code> asyncio::net::dgram::Socket::bind(const Address &address) {
    return socketAddressFrom(address).and_then([this](const auto &addr) -> tl::expected<void, std::error_code> {
        if (::bind(mFD, addr.first.get(), addr.second) != 0)
            return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

        return {};
    });
}

zero::async::coroutine::Task<void, std::error_code> asyncio::net::dgram::Socket::connect(const Address address) {
    co_return socketAddressFrom(address).and_then([this](const auto &addr) -> tl::expected<void, std::error_code> {
        if (::connect(mFD, addr.first.get(), addr.second) != 0)
            return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

        return {};
    });
}

asyncio::FileDescriptor asyncio::net::dgram::Socket::fd() const {
    return mFD;
}

tl::expected<asyncio::net::dgram::Socket, std::error_code> asyncio::net::dgram::bind(const Address &address) {
    auto socket = Socket::make(std::holds_alternative<IPv4Address>(address) ? AF_INET : AF_INET6);
    EXPECT(socket);
    EXPECT(socket->bind(address));
    return *std::move(socket);
}

tl::expected<asyncio::net::dgram::Socket, std::error_code>
asyncio::net::dgram::bind(const std::span<const Address> addresses) {
    if (addresses.empty())
        return tl::unexpected(IOError::INVALID_ARGUMENT);

    auto it = addresses.begin();

    while (true) {
        auto result = dgram::bind(*it);

        if (result)
            return *std::move(result);

        if (++it == addresses.end())
            return tl::unexpected(result.error());
    }
}

tl::expected<asyncio::net::dgram::Socket, std::error_code>
asyncio::net::dgram::bind(const std::string &ip, const unsigned short port) {
    return addressFrom(ip, port).and_then([](const auto &address) {
        return dgram::bind(address);
    });
}

zero::async::coroutine::Task<asyncio::net::dgram::Socket, std::error_code>
asyncio::net::dgram::connect(const Address address) {
    auto socket = Socket::make(std::holds_alternative<IPv4Address>(address) ? AF_INET : AF_INET6);
    CO_EXPECT(socket);
    CO_EXPECT(co_await socket->connect(address));
    co_return *std::move(socket);
}

zero::async::coroutine::Task<asyncio::net::dgram::Socket, std::error_code>
asyncio::net::dgram::connect(const std::span<const Address> addresses) {
    if (addresses.empty())
        co_return tl::unexpected(IOError::INVALID_ARGUMENT);

    auto it = addresses.begin();

    while (true) {
        auto result = co_await connect(*it);

        if (result)
            co_return *std::move(result);

        if (++it == addresses.end())
            co_return tl::unexpected(result.error());
    }
}

zero::async::coroutine::Task<asyncio::net::dgram::Socket, std::error_code>
asyncio::net::dgram::connect(const std::string host, const unsigned short port) {
    dns::AddressInfo hints = {};

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    const auto result = co_await dns::getAddressInfo(host, std::to_string(port), hints);
    CO_EXPECT(result);

    co_return co_await connect(*result);
}

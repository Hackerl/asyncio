#include <asyncio/net/dgram.h>
#include <asyncio/net/dns.h>
#include <asyncio/error.h>
#include <zero/try.h>
#include <cassert>

constexpr auto READ_INDEX = 0;
constexpr auto WRITE_INDEX = 1;

asyncio::net::dgram::Socket::Socket(const FileDescriptor fd, std::array<ev::Event, 2> events)
    : mClosed(false), mFD(fd), mEvents(std::move(events)) {
}

asyncio::net::dgram::Socket::Socket(Socket &&rhs) noexcept
    : mClosed(rhs.mClosed), mFD(std::exchange(rhs.mFD, INVALID_FILE_DESCRIPTOR)),
      mEvents(std::move(rhs.mEvents)), mTimeouts(rhs.mTimeouts) {
    assert(!mEvents[READ_INDEX].pending());
    assert(!mEvents[WRITE_INDEX].pending());
}

asyncio::net::dgram::Socket::~Socket() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        return;

    evutil_closesocket(mFD);
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::net::dgram::Socket::read(std::span<std::byte> data) {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mClosed)
        co_return tl::unexpected(IO_EOF);

    if (mEvents[READ_INDEX].pending())
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    tl::expected<std::size_t, std::error_code> result;

    while (true) {
#ifdef _WIN32
        const int num = recv(mFD, reinterpret_cast<char *>(data.data()), static_cast<int>(data.size()), 0);

        if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
#else
        const ssize_t num = recv(mFD, data.data(), data.size(), 0);

        if (num == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
            break;
        }
#endif

        if (num == 0) {
            result = tl::unexpected<std::error_code>(IO_EOF);
            break;
        }

        if (num > 0) {
            *result = num;
            break;
        }

        const auto what = co_await mEvents[READ_INDEX].on(mTimeouts[READ_INDEX]);

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }

        if (*what & ev::What::CLOSED) {
            result = tl::unexpected<std::error_code>(IO_EOF);
            break;
        }

        if (*what & ev::What::TIMEOUT) {
            result = tl::unexpected(make_error_code(std::errc::timed_out));
            break;
        }
    }

    co_return result;
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::net::dgram::Socket::write(std::span<const std::byte> data) {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mClosed)
        co_return tl::unexpected(IO_EOF);

    if (mEvents[WRITE_INDEX].pending())
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    tl::expected<std::size_t, std::error_code> result;

    while (true) {
#ifdef _WIN32
        const int num = send(mFD, reinterpret_cast<const char *>(data.data()), static_cast<int>(data.size()), 0);

        if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
#else
        const ssize_t num = send(mFD, data.data(), data.size(), 0);

        if (num == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
            break;
        }
#endif

        if (num == 0) {
            result = tl::unexpected<std::error_code>(IO_EOF);
            break;
        }

        if (num > 0) {
            *result = num;
            break;
        }

        const auto what = co_await mEvents[WRITE_INDEX].on(mTimeouts[WRITE_INDEX]);

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }

        if (*what & ev::What::CLOSED) {
            result = tl::unexpected<std::error_code>(IO_EOF);
            break;
        }

        if (*what & ev::What::TIMEOUT) {
            result = tl::unexpected(make_error_code(std::errc::timed_out));
            break;
        }
    }

    co_return result;
}

zero::async::coroutine::Task<void, std::error_code> asyncio::net::dgram::Socket::close() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mClosed)
        co_return tl::unexpected(IO_EOF);

    mClosed = true;

    for (auto &event: mEvents) {
        if (!event.pending())
            continue;

        event.trigger(ev::What::CLOSED);
    }

    if (evutil_closesocket(std::exchange(mFD, INVALID_FILE_DESCRIPTOR)) != 0)
        co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    co_return tl::expected<void, std::error_code>{};
}

zero::async::coroutine::Task<std::pair<std::size_t, asyncio::net::Address>, std::error_code>
asyncio::net::dgram::Socket::readFrom(std::span<std::byte> data) {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mClosed)
        co_return tl::unexpected(IO_EOF);

    if (mEvents[READ_INDEX].pending())
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    tl::expected<std::pair<std::size_t, Address>, std::error_code> result;

    while (true) {
        sockaddr_storage storage = {};
        socklen_t length = sizeof(sockaddr_storage);

#ifdef _WIN32
        const int num = recvfrom(
            mFD,
            reinterpret_cast<char *>(data.data()),
            static_cast<int>(data.size()),
            0,
            reinterpret_cast<sockaddr *>(&storage),
            &length
        );

        if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
#else
        const ssize_t num = recvfrom(mFD, data.data(), data.size(), 0, reinterpret_cast<sockaddr *>(&storage), &length);

        if (num == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
            break;
        }
#endif

        if (num == 0) {
            result = tl::unexpected<std::error_code>(IO_EOF);
            break;
        }

        if (num > 0) {
            const auto address = addressFrom(reinterpret_cast<sockaddr *>(&storage), length);

            if (!address) {
                result = tl::unexpected(address.error());
                break;
            }

            result = {num, *address};
            break;
        }

        const auto what = co_await mEvents[READ_INDEX].on(mTimeouts[READ_INDEX]);

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }

        if (*what & ev::What::CLOSED) {
            result = tl::unexpected<std::error_code>(IO_EOF);
            break;
        }

        if (*what & ev::What::TIMEOUT) {
            result = tl::unexpected(make_error_code(std::errc::timed_out));
            break;
        }
    }

    co_return result;
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::net::dgram::Socket::writeTo(std::span<const std::byte> data, const Address address) {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mClosed)
        co_return tl::unexpected(IO_EOF);

    if (mEvents[WRITE_INDEX].pending())
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    const auto socketAddress = CO_TRY(socketAddressFrom(address));

    tl::expected<std::size_t, std::error_code> result;

    while (true) {
#ifdef _WIN32
        const int num = sendto(
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

        if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
#else
        const ssize_t num = sendto(
            mFD,
            data.data(),
            data.size(),
            0,
            socketAddress->first.get(),
            socketAddress->second
        );

        if (num == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
            break;
        }
#endif

        if (num == 0) {
            result = tl::unexpected<std::error_code>(IO_EOF);
            break;
        }

        if (num > 0) {
            *result = num;
            break;
        }

        const auto what = co_await mEvents[WRITE_INDEX].on(mTimeouts[WRITE_INDEX]);

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }

        if (*what & ev::What::CLOSED) {
            result = tl::unexpected<std::error_code>(IO_EOF);
            break;
        }

        if (*what & ev::What::TIMEOUT) {
            result = tl::unexpected(make_error_code(std::errc::timed_out));
            break;
        }
    }

    co_return result;
}

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::dgram::Socket::localAddress() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    return addressFrom(mFD, false);
}

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::dgram::Socket::remoteAddress() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    return addressFrom(mFD, true);
}


tl::expected<void, std::error_code> asyncio::net::dgram::Socket::bind(const Address &address) {
    return socketAddressFrom(address).and_then([this](const auto &addr) -> tl::expected<void, std::error_code> {
        if (::bind(mFD, addr.first.get(), addr.second) != 0)
            return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

        return {};
    });
}

zero::async::coroutine::Task<void, std::error_code> asyncio::net::dgram::Socket::connect(const Address address) {
    co_return socketAddressFrom(address).and_then([this](const auto &addr) -> tl::expected<void, std::error_code> {
        if (::connect(mFD, addr.first.get(), addr.second) != 0)
            return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

        return {};
    });
}

void asyncio::net::dgram::Socket::setTimeout(const std::chrono::milliseconds timeout) {
    setTimeout(timeout, timeout);
}

void asyncio::net::dgram::Socket::setTimeout(
    std::chrono::milliseconds readTimeout,
    std::chrono::milliseconds writeTimeout
) {
    if (readTimeout != std::chrono::milliseconds::zero())
        mTimeouts[READ_INDEX] = readTimeout;
    else
        mTimeouts[READ_INDEX].reset();

    if (writeTimeout != std::chrono::milliseconds::zero())
        mTimeouts[WRITE_INDEX] = writeTimeout;
    else
        mTimeouts[WRITE_INDEX].reset();
}

asyncio::FileDescriptor asyncio::net::dgram::Socket::fd() {
    return mFD;
}

tl::expected<asyncio::net::dgram::Socket, std::error_code> asyncio::net::dgram::bind(const Address &address) {
    auto socket = TRY(makeSocket(address.index() == 0 ? AF_INET : AF_INET6));
    TRY(socket->bind(address));
    return socket;
}

tl::expected<asyncio::net::dgram::Socket, std::error_code>
asyncio::net::dgram::bind(std::span<const Address> addresses) {
    if (addresses.empty())
        return tl::unexpected(make_error_code(std::errc::invalid_argument));

    auto it = addresses.begin();

    while (true) {
        auto result = dgram::bind(*it);

        if (result)
            return result;

        if (++it == addresses.end())
            return tl::unexpected(result.error());
    }
}

tl::expected<asyncio::net::dgram::Socket, std::error_code>
asyncio::net::dgram::bind(const std::string &ip, const unsigned short port) {
    auto address = TRY(addressFrom(ip, port));
    return dgram::bind(*address);
}

zero::async::coroutine::Task<asyncio::net::dgram::Socket, std::error_code>
asyncio::net::dgram::connect(const Address address) {
    auto socket = CO_TRY(makeSocket(address.index() == 0 ? AF_INET : AF_INET6));
    CO_TRY(co_await socket->connect(address));
    co_return std::move(*socket);
}

zero::async::coroutine::Task<asyncio::net::dgram::Socket, std::error_code>
asyncio::net::dgram::connect(std::span<const Address> addresses) {
    if (addresses.empty())
        co_return tl::unexpected(make_error_code(std::errc::invalid_argument));

    auto it = addresses.begin();

    while (true) {
        auto result = std::move(co_await connect(*it));

        if (result)
            co_return std::move(*result);

        if (++it == addresses.end())
            co_return tl::unexpected(result.error());
    }
}

zero::async::coroutine::Task<asyncio::net::dgram::Socket, std::error_code>
asyncio::net::dgram::connect(const std::string host, const unsigned short port) {
    dns::AddressInfo hints = {};

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    auto result = CO_TRY(co_await dns::getAddressInfo(host, std::to_string(port), hints));
    co_return std::move(co_await connect(*result));
}

tl::expected<asyncio::net::dgram::Socket, std::error_code> asyncio::net::dgram::makeSocket(const int family) {
#ifdef _WIN32
    const auto fd = static_cast<FileDescriptor>(socket(family, SOCK_DGRAM, 0));
#else
    const auto fd = socket(family, SOCK_DGRAM, 0);
#endif

    if (fd == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    if (evutil_make_socket_nonblocking(fd) == -1) {
        evutil_closesocket(fd);
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }

    auto events = std::array{
        makeEvent(fd, ev::What::READ),
        makeEvent(fd, ev::What::WRITE)
    };

    if (!events[0]) {
        evutil_closesocket(fd);
        return tl::unexpected(events[0].error());
    }

    if (!events[1]) {
        evutil_closesocket(fd);
        return tl::unexpected(events[1].error());
    }

    return Socket{fd, {std::move(*events[0]), std::move(*events[1])}};
}

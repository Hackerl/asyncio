#include <asyncio/net/dgram.h>
#include <asyncio/net/dns.h>
#include <asyncio/error.h>
#include <cassert>

constexpr auto READ_INDEX = 0;
constexpr auto WRITE_INDEX = 1;

asyncio::net::dgram::Socket::Socket(evutil_socket_t fd, std::array<ev::Event, 2> events)
        : mFD(fd), mClosed(false), mEvents(std::move(events)) {

}

asyncio::net::dgram::Socket::Socket(asyncio::net::dgram::Socket &&rhs) noexcept
        : mFD(std::exchange(rhs.mFD, EVUTIL_INVALID_SOCKET)), mClosed(rhs.mClosed),
          mEvents(std::move(rhs.mEvents)), mTimeouts(rhs.mTimeouts) {
    assert(!mEvents[READ_INDEX].pending());
    assert(!mEvents[WRITE_INDEX].pending());
}

asyncio::net::dgram::Socket::~Socket() {
    if (mFD == EVUTIL_INVALID_SOCKET)
        return;

    evutil_closesocket(mFD);
}

zero::async::coroutine::Task<size_t, std::error_code> asyncio::net::dgram::Socket::read(std::span<std::byte> data) {
    if (mFD == EVUTIL_INVALID_SOCKET)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mClosed)
        co_return tl::unexpected(Error::IO_EOF);

    if (mEvents[READ_INDEX].pending())
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    tl::expected<size_t, std::error_code> result;

    while (true) {
#ifdef _WIN32
        int num = recv(mFD, (char *) data.data(), (int) data.size(), 0);

        if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
#else
        ssize_t num = recv(mFD, data.data(), data.size(), 0);

        if (num == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
            break;
        }
#endif

        if (num == 0) {
            result = tl::unexpected<std::error_code>(Error::IO_EOF);
            break;
        }

        if (num > 0) {
            result = num;
            break;
        }

        auto what = co_await mEvents[READ_INDEX].on(mTimeouts[READ_INDEX]);

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }

        if (*what & ev::What::CLOSED) {
            result = tl::unexpected<std::error_code>(Error::IO_EOF);
            break;
        } else if (*what & ev::What::TIMEOUT) {
            result = tl::unexpected(make_error_code(std::errc::timed_out));
            break;
        }
    }

    co_return result;
}

zero::async::coroutine::Task<void, std::error_code>
asyncio::net::dgram::Socket::write(std::span<const std::byte> data) {
    if (mFD == EVUTIL_INVALID_SOCKET)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mClosed)
        co_return tl::unexpected(Error::IO_EOF);

    if (mEvents[WRITE_INDEX].pending())
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    tl::expected<void, std::error_code> result;

    while (true) {
#ifdef _WIN32
        int num = send(mFD, (const char *) data.data(), (int) data.size(), 0);

        if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
#else
        ssize_t num = send(mFD, data.data(), data.size(), 0);

        if (num == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
            break;
        }
#endif

        if (num == 0) {
            result = tl::unexpected<std::error_code>(Error::IO_EOF);
            break;
        }

        if (num > 0)
            break;

        auto what = co_await mEvents[WRITE_INDEX].on(mTimeouts[WRITE_INDEX]);

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }

        if (*what & ev::What::CLOSED) {
            result = tl::unexpected<std::error_code>(Error::IO_EOF);
            break;
        } else if (*what & ev::What::TIMEOUT) {
            result = tl::unexpected(make_error_code(std::errc::timed_out));
            break;
        }
    }

    co_return result;
}

tl::expected<void, std::error_code> asyncio::net::dgram::Socket::close() {
    if (mFD == EVUTIL_INVALID_SOCKET)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mClosed)
        return tl::unexpected(Error::IO_EOF);

    for (auto &event: mEvents) {
        if (!event.pending())
            continue;

        event.trigger(ev::What::CLOSED);
    }

    evutil_closesocket(std::exchange(mFD, EVUTIL_INVALID_SOCKET));
    return {};
}

zero::async::coroutine::Task<std::pair<size_t, asyncio::net::Address>, std::error_code>
asyncio::net::dgram::Socket::readFrom(std::span<std::byte> data) {
    if (mFD == EVUTIL_INVALID_SOCKET)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mClosed)
        co_return tl::unexpected(Error::IO_EOF);

    if (mEvents[READ_INDEX].pending())
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    tl::expected<std::pair<size_t, asyncio::net::Address>, std::error_code> result;

    while (true) {
        sockaddr_storage storage = {};
        socklen_t length = sizeof(sockaddr_storage);

#ifdef _WIN32
        int num = recvfrom(mFD, (char *) data.data(), (int) data.size(), 0, (sockaddr *) &storage, &length);

        if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
#else
        ssize_t num = recvfrom(mFD, data.data(), data.size(), 0, (sockaddr *) &storage, &length);

        if (num == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
            break;
        }
#endif

        if (num == 0) {
            result = tl::unexpected<std::error_code>(Error::IO_EOF);
            break;
        }

        if (num > 0) {
            auto address = addressFrom((const sockaddr *) &storage);

            if (!address) {
                result = tl::unexpected(address.error());
                break;
            }

            result = {num, *address};
            break;
        }

        auto what = co_await mEvents[READ_INDEX].on(mTimeouts[READ_INDEX]);

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }

        if (*what & ev::What::CLOSED) {
            result = tl::unexpected<std::error_code>(Error::IO_EOF);
            break;
        } else if (*what & ev::What::TIMEOUT) {
            result = tl::unexpected(make_error_code(std::errc::timed_out));
            break;
        }
    }

    co_return result;
}

zero::async::coroutine::Task<void, std::error_code>
asyncio::net::dgram::Socket::writeTo(std::span<const std::byte> data, const Address &address) {
    if (mFD == EVUTIL_INVALID_SOCKET)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mClosed)
        co_return tl::unexpected(Error::IO_EOF);

    if (mEvents[WRITE_INDEX].pending())
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    auto socketAddress = socketAddressFrom(address);

    if (!socketAddress)
        co_return tl::unexpected(socketAddress.error());

    tl::expected<void, std::error_code> result;

    while (true) {
#ifdef _WIN32
        int num = sendto(mFD,
                (const char *) data.data(),
                (int) data.size(),
                0,
                (const sockaddr *) &*socketAddress,
                sizeof(sockaddr_storage)
        );

        if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
#else
        ssize_t num = sendto(
                mFD,
                data.data(),
                data.size(),
                0,
                (const sockaddr *) &*socketAddress,
                sizeof(sockaddr_storage)
        );

        if (num == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
            break;
        }
#endif

        if (num == 0) {
            result = tl::unexpected<std::error_code>(Error::IO_EOF);
            break;
        }

        if (num > 0)
            break;

        auto what = co_await mEvents[WRITE_INDEX].on(mTimeouts[WRITE_INDEX]);

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }

        if (*what & ev::What::CLOSED) {
            result = tl::unexpected<std::error_code>(Error::IO_EOF);
            break;
        } else if (*what & ev::What::TIMEOUT) {
            result = tl::unexpected(make_error_code(std::errc::timed_out));
            break;
        }
    }

    co_return result;
}

void asyncio::net::dgram::Socket::setTimeout(std::chrono::milliseconds timeout) {
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

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::dgram::Socket::localAddress() {
    if (mFD == EVUTIL_INVALID_SOCKET)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    return addressFrom(mFD, false);
}

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::dgram::Socket::remoteAddress() {
    if (mFD == EVUTIL_INVALID_SOCKET)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    return addressFrom(mFD, true);
}

evutil_socket_t asyncio::net::dgram::Socket::fd() {
    return mFD;
}

tl::expected<void, std::error_code> asyncio::net::dgram::Socket::bind(const asyncio::net::Address &address) {
    auto socketAddress = socketAddressFrom(address);

    if (!socketAddress)
        return tl::unexpected(socketAddress.error());

    if (::bind(mFD, (const sockaddr *) &*socketAddress, sizeof(sockaddr_storage)) != 0)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return {};
}

zero::async::coroutine::Task<void, std::error_code> asyncio::net::dgram::Socket::connect(const Address &address) {
    auto socketAddress = socketAddressFrom(address);

    if (!socketAddress)
        co_return tl::unexpected(socketAddress.error());

    if (::connect(mFD, (const sockaddr *) &*socketAddress, sizeof(sockaddr_storage)) != 0)
        co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    co_return tl::expected<void, std::error_code>{};
}

tl::expected<asyncio::net::dgram::Socket, std::error_code> asyncio::net::dgram::bind(const Address &address) {
    auto socket = makeSocket(address.index() == 0 ? AF_INET : AF_INET6);

    if (!socket)
        return tl::unexpected(socket.error());

    auto result = socket->bind(address);

    if (!result)
        return tl::unexpected(result.error());

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

        if (it == addresses.end())
            return tl::unexpected(result.error());

        it++;
    }
}

tl::expected<asyncio::net::dgram::Socket, std::error_code>
asyncio::net::dgram::bind(const std::string &ip, unsigned short port) {
    auto address = addressFrom(ip, port);

    if (!address)
        return tl::unexpected(address.error());

    return dgram::bind(*address);
}

zero::async::coroutine::Task<std::shared_ptr<asyncio::net::dgram::Socket>, std::error_code>
asyncio::net::dgram::connect(const asyncio::net::Address &address) {
    auto socket = makeSocket(address.index() == 0 ? AF_INET : AF_INET6);

    if (!socket)
        co_return tl::unexpected(socket.error());

    auto result = co_await socket->connect(address);

    if (!result)
        co_return tl::unexpected(result.error());

    co_return std::make_shared<Socket>(std::move(*socket));
}

zero::async::coroutine::Task<std::shared_ptr<asyncio::net::dgram::Socket>, std::error_code>
asyncio::net::dgram::connect(std::span<const Address> addresses) {
    if (addresses.empty())
        co_return tl::unexpected(make_error_code(std::errc::invalid_argument));

    auto it = addresses.begin();

    while (true) {
        auto result = co_await connect(*it);

        if (result)
            co_return result;

        if (it == addresses.end())
            co_return tl::unexpected(result.error());

        it++;
    }
}

zero::async::coroutine::Task<std::shared_ptr<asyncio::net::dgram::Socket>, std::error_code>
asyncio::net::dgram::connect(const std::string &host, unsigned short port) {
    evutil_addrinfo hints = {};

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    auto result = co_await dns::getAddressInfo(host, std::to_string(port), hints);

    if (!result)
        co_return tl::unexpected(result.error());

    co_return co_await connect(*result);
}

tl::expected<asyncio::net::dgram::Socket, std::error_code> asyncio::net::dgram::makeSocket(int family) {
    auto fd = (evutil_socket_t) socket(family, SOCK_DGRAM, 0);

    if (fd == EVUTIL_INVALID_SOCKET)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    if (evutil_make_socket_nonblocking(fd) == -1) {
        evutil_closesocket(fd);
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }

    auto events = std::array{
            ev::makeEvent(fd, ev::What::READ),
            ev::makeEvent(fd, ev::What::WRITE)
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

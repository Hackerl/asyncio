#include <asyncio/net/dgram.h>
#include <asyncio/error.h>
#include <cassert>

constexpr auto READ_INDEX = 0;
constexpr auto WRITE_INDEX = 1;

asyncio::net::dgram::Socket::Socket(evutil_socket_t fd, asyncio::ev::Event *events)
        : mFD(fd), mClosed(false), mEvents{std::move(events[0]), std::move(events[1])} {

}

asyncio::net::dgram::Socket::Socket(asyncio::net::dgram::Socket &&rhs) noexcept
        : mFD(std::exchange(rhs.mFD, EVUTIL_INVALID_SOCKET)), mClosed(rhs.mClosed),
          mEvents(std::move(rhs.mEvents)), mTimeouts(rhs.mTimeouts) {
    assert(!mEvents[READ_INDEX].pending());
    assert(!mEvents[WRITE_INDEX].pending());
}

asyncio::net::dgram::Socket::~Socket() {
    if (mFD != EVUTIL_INVALID_SOCKET)
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

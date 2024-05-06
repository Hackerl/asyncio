#include <asyncio/fs/pipe.h>

#ifndef _WIN32
#include <unistd.h>
#include <algorithm>
#include <zero/defer.h>
#include <zero/expect.h>
#endif

#ifndef _WIN32
constexpr auto READ_INDEX = 0;
constexpr auto WRITE_INDEX = 1;
#endif

#ifdef _WIN32
asyncio::fs::Pipe::Pipe(const FileDescriptor fd) : mFD(fd) {
}

asyncio::fs::Pipe::Pipe(Pipe &&rhs) noexcept: mFD(std::exchange(rhs.mFD, INVALID_FILE_DESCRIPTOR)) {
}

asyncio::fs::Pipe &asyncio::fs::Pipe::operator=(Pipe &&rhs) noexcept {
    mFD = std::exchange(rhs.mFD, INVALID_FILE_DESCRIPTOR);
    return *this;
}
#else
asyncio::fs::Pipe::Pipe(const FileDescriptor fd, std::array<std::optional<ev::Event>, 2> events)
    : mFD(fd), mEvents(std::move(events)) {
}

asyncio::fs::Pipe::Pipe(Pipe &&rhs) noexcept
    : mFD(std::exchange(rhs.mFD, INVALID_FILE_DESCRIPTOR)), mEvents(std::move(rhs.mEvents)) {
}

asyncio::fs::Pipe &asyncio::fs::Pipe::operator=(Pipe &&rhs) noexcept {
    mFD = std::exchange(rhs.mFD, INVALID_FILE_DESCRIPTOR);
    mEvents = std::move(rhs.mEvents);
    return *this;
}
#endif

asyncio::fs::Pipe::~Pipe() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        return;

#ifdef _WIN32
    CloseHandle(reinterpret_cast<HANDLE>(mFD));
#else
    ::close(mFD);
#endif
}

tl::expected<asyncio::fs::Pipe, std::error_code> asyncio::fs::Pipe::from(const FileDescriptor fd) {
#ifdef _WIN32
    if (GetFileType(reinterpret_cast<HANDLE>(fd)) != FILE_TYPE_PIPE)
        return tl::unexpected(IOError::BAD_FILE_DESCRIPTOR);

    return Pipe{fd};
#else
    if (evutil_make_socket_nonblocking(fd) == -1)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    std::array events = {
        ev::Event::make(fd, ev::What::READ),
        ev::Event::make(fd, ev::What::WRITE)
    };

    EXPECT(events[0]);
    EXPECT(events[1]);

    return Pipe{fd, {*std::move(events[0]), *std::move(events[1])}};
#endif
}

zero::async::coroutine::Task<void, std::error_code> asyncio::fs::Pipe::close() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(IOError::BAD_FILE_DESCRIPTOR);

#ifdef _WIN32
    if (!CloseHandle(reinterpret_cast<HANDLE>(std::exchange(mFD, INVALID_FILE_DESCRIPTOR))))
        co_return tl::unexpected<std::error_code>(static_cast<int>(GetLastError()), std::system_category());
#else
    assert(std::ranges::all_of(mEvents, [](const auto &event) {return !event || !event->pending();}));

    if (::close(std::exchange(mFD, INVALID_FILE_DESCRIPTOR)) != 0)
        co_return tl::unexpected<std::error_code>(errno, std::system_category());
#endif

    co_return {};
}

zero::async::coroutine::Task<std::size_t, std::error_code> asyncio::fs::Pipe::read(const std::span<std::byte> data) {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(IOError::BAD_FILE_DESCRIPTOR);

#ifdef _WIN32
    co_return co_await toThread(
        [=, this]() -> tl::expected<std::size_t, std::error_code> {
            DWORD n;

            if (!ReadFile(reinterpret_cast<HANDLE>(mFD), data.data(), data.size(), &n, nullptr))
                return tl::unexpected<std::error_code>(static_cast<int>(GetLastError()), std::system_category());

            return n;
        },
        [this](std::thread::native_handle_type) -> tl::expected<void, std::error_code> {
            if (!CancelIoEx(reinterpret_cast<HANDLE>(mFD), nullptr))
                return tl::unexpected<std::error_code>(static_cast<int>(GetLastError()), std::system_category());

            return {};
        }
    ).orElse([](const auto &ec) -> tl::expected<std::size_t, std::error_code> {
        if (ec == std::errc::broken_pipe)
            return 0;

        return tl::unexpected(ec);
    });
#else
    if (!mEvents[READ_INDEX])
        co_return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

    tl::expected<size_t, std::error_code> result;

    while (true) {
        const ssize_t n = ::read(mFD, data.data(), data.size());

        if (n == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected<std::error_code>(errno, std::system_category());
            break;
        }

        if (n >= 0) {
            result = n;
            break;
        }

        const auto what = co_await mEvents[READ_INDEX]->on();

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }

        assert(*what & ev::What::READ);
    }

    co_return result;
#endif
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::fs::Pipe::write(const std::span<const std::byte> data) {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(IOError::BAD_FILE_DESCRIPTOR);

#ifdef _WIN32
    co_return co_await toThread(
        [=, this]() -> tl::expected<std::size_t, std::error_code> {
            tl::expected<size_t, std::error_code> result;

            while (*result < data.size()) {
                DWORD n;

                if (!WriteFile(reinterpret_cast<HANDLE>(mFD), data.data(), data.size(), &n, nullptr)) {
                    result = tl::unexpected<std::error_code>(static_cast<int>(GetLastError()), std::system_category());
                    break;
                }

                *result += n;
            }

            return result;
        },
        [this](std::thread::native_handle_type) -> tl::expected<void, std::error_code> {
            if (!CancelIoEx(reinterpret_cast<HANDLE>(mFD), nullptr))
                return tl::unexpected<std::error_code>(static_cast<int>(GetLastError()), std::system_category());

            return {};
        }
    ).transformError([](const auto &ec) -> std::error_code {
        if (ec == std::error_code{ERROR_NO_DATA, std::system_category()})
            return IOError::BROKEN_PIPE;

        return ec;
    });
#else
    if (!mEvents[WRITE_INDEX])
        co_return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

    tl::expected<size_t, std::error_code> result;

    while (*result < data.size()) {
        const ssize_t n = ::write(mFD, data.data() + *result, data.size() - *result);

        if (n == -1 && errno != EWOULDBLOCK) {
            if (*result > 0)
                break;

            result = tl::unexpected<std::error_code>(errno, std::system_category());
            break;
        }

        assert(n != 0);

        if (n > 0) {
            *result += n;
            continue;
        }

        const auto what = co_await mEvents[WRITE_INDEX]->on();

        if (!what) {
            result = tl::unexpected(what.error());
            break;
        }

        assert(*what & ev::What::WRITE);
    }

    co_return result;
#endif
}

asyncio::FileDescriptor asyncio::fs::Pipe::fd() const {
    return mFD;
}

tl::expected<std::array<asyncio::fs::Pipe, 2>, std::error_code> asyncio::fs::pipe() {
#ifdef _WIN32
    HANDLE readPipe, writePipe;

    if (!CreatePipe(&readPipe, &writePipe, nullptr, 0))
        return tl::unexpected<std::error_code>(static_cast<int>(GetLastError()), std::system_category());

    return std::array{
        Pipe{reinterpret_cast<FileDescriptor>(readPipe)},
        Pipe{reinterpret_cast<FileDescriptor>(writePipe)}
    };
#else
    int fds[2];
    std::ranges::fill(fds, -1);

    DEFER(
        for (const auto &fd : fds) {
            if (fd < 0)
                continue;

            close(fd);
        }
    );

    if (::pipe(fds) < 0)
        return tl::unexpected<std::error_code>(errno, std::system_category());

    if (evutil_make_socket_nonblocking(fds[0]) == -1 || evutil_make_socket_nonblocking(fds[1]) == -1)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    std::array events = {
        ev::Event::make(fds[0], ev::What::READ),
        ev::Event::make(fds[1], ev::What::WRITE)
    };

    EXPECT(events[0]);
    EXPECT(events[1]);

    return std::array{
        Pipe{std::exchange(fds[0], -1), {*std::move(events[0]), std::nullopt}},
        Pipe{std::exchange(fds[1], -1), {std::nullopt, *std::move(events[1])}}
    };
#endif
}

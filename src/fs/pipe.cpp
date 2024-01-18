#include <asyncio/fs/pipe.h>
#include <asyncio/error.h>

#ifdef _WIN32
#include <asyncio/thread.h>
#else
#include <unistd.h>
#include <zero/expect.h>
#endif

#ifdef _WIN32
asyncio::fs::Pipe::Pipe(const FileDescriptor fd): mFD(fd) {
}

asyncio::fs::Pipe::Pipe(Pipe &&rhs) noexcept: mFD(std::exchange(rhs.mFD, INVALID_FILE_DESCRIPTOR)) {
}
#else
asyncio::fs::Pipe::Pipe(const FileDescriptor fd, ev::Event event): mFD(fd), mEvent(std::move(event)) {
}

asyncio::fs::Pipe::Pipe(Pipe &&rhs) noexcept
    : mFD(std::exchange(rhs.mFD, INVALID_FILE_DESCRIPTOR)), mEvent(std::move(rhs.mEvent)) {
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
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    return Pipe{fd};
#else
    if (evutil_make_socket_nonblocking(fd) == -1)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    auto event = ev::makeEvent(fd,  ev::What::READ | ev::What::WRITE);
    EXPECT(event);

    return Pipe{fd, std::move(*event)};
#endif
}

zero::async::coroutine::Task<void, std::error_code> asyncio::fs::Pipe::close() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

#ifdef _WIN32
    if (!CloseHandle(reinterpret_cast<HANDLE>(std::exchange(mFD, INVALID_FILE_DESCRIPTOR))))
        co_return tl::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
#else
    assert(!mEvent.pending());

    if (::close(std::exchange(mFD, INVALID_FILE_DESCRIPTOR)) != 0)
        co_return tl::unexpected(std::error_code(errno, std::system_category()));
#endif

    co_return tl::expected<void, std::error_code>{};
}

zero::async::coroutine::Task<std::size_t, std::error_code> asyncio::fs::Pipe::read(std::span<std::byte> data) {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

#ifdef _WIN32
    co_return co_await toThread(
        [=]() -> tl::expected<std::size_t, std::error_code> {
            DWORD n;

            if (!ReadFile(reinterpret_cast<HANDLE>(mFD), data.data(), data.size(), &n, nullptr))
                return tl::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));

            return n;
        },
        [this](std::thread::native_handle_type) -> tl::expected<void, std::error_code> {
            if (!CancelIoEx(reinterpret_cast<HANDLE>(mFD), nullptr))
                return tl::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));

            return {};
        }
    ).transformError([](const auto &ec) -> std::error_code {
        if (ec == std::errc::broken_pipe)
            return IO_EOF;

        return ec;
    });
#else
    tl::expected<size_t, std::error_code> result;

    while (true) {
        const ssize_t n = ::read(mFD, data.data(), data.size());

        if (n == -1 && errno != EWOULDBLOCK) {
            result = tl::unexpected(std::error_code(errno, std::system_category()));
            break;
        }

        if (n == 0) {
            result = tl::unexpected<std::error_code>(IO_EOF);
            break;
        }

        if (n > 0) {
            result = n;
            break;
        }

        const auto what = co_await mEvent.on();

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
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

#ifdef _WIN32
    co_return co_await toThread(
        [=]() -> tl::expected<std::size_t, std::error_code> {
            tl::expected<size_t, std::error_code> result;

            while (*result < data.size()) {
                DWORD n;

                if (!WriteFile(reinterpret_cast<HANDLE>(mFD), data.data(), data.size(), &n, nullptr)) {
                    result = tl::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
                    break;
                }

                *result += n;
            }

            return result;
        },
        [this](std::thread::native_handle_type) -> tl::expected<void, std::error_code> {
            if (!CancelIoEx(reinterpret_cast<HANDLE>(mFD), nullptr))
                return tl::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));

            return {};
        }
    ).transformError([](const auto &ec) -> std::error_code {
        if (ec == std::error_code{ERROR_NO_DATA, std::system_category()})
            return make_error_code(std::errc::broken_pipe);

        return ec;
    });
#else
    tl::expected<size_t, std::error_code> result;

    while (*result < data.size()) {
        const ssize_t n = ::write(mFD, data.data() + *result, data.size() - *result);

        if (n == -1 && errno != EWOULDBLOCK) {
            if (*result > 0)
                break;

            result = tl::unexpected(std::error_code(errno, std::system_category()));
            break;
        }

        if (n == 0) {
            result = tl::unexpected<std::error_code>(IO_EOF);
            break;
        }

        if (n > 0) {
            *result += n;
            continue;
        }

        const auto what = co_await mEvent.on();

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
        return tl::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));

    return std::array{
        Pipe{reinterpret_cast<FileDescriptor>(readPipe)},
        Pipe{reinterpret_cast<FileDescriptor>(writePipe)}
    };
#else
    int fds[2];

    if (::pipe(fds) < 0)
        return tl::unexpected(std::error_code(errno, std::system_category()));

    if (evutil_make_socket_nonblocking(fds[0]) == -1 || evutil_make_socket_nonblocking(fds[1]) == -1) {
        close(fds[0]);
        close(fds[1]);
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }

    auto events = std::array{
        ev::makeEvent(fds[0], ev::What::READ | ev::What::WRITE),
        ev::makeEvent(fds[1], ev::What::READ | ev::What::WRITE)
    };

    if (!events[0]) {
        close(fds[0]);
        close(fds[1]);
        return tl::unexpected(events[0].error());
    }

    if (!events[1]) {
        close(fds[0]);
        close(fds[1]);
        return tl::unexpected(events[1].error());
    }

    return std::array{
        Pipe{fds[0], std::move(*events[0])},
        Pipe{fds[1], std::move(*events[1])}
    };
#endif
}

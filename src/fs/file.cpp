#include <asyncio/fs/file.h>
#include <asyncio/event_loop.h>
#include <asyncio/error.h>
#include <cassert>
#include <fcntl.h>

#ifndef _WIN32
#include <unistd.h>
#endif

asyncio::fs::File::File(std::shared_ptr<EventLoop> eventLoop, const FileDescriptor fd, const bool append)
    : mAppend(append), mFD(fd), mOffset(0), mEventLoop(std::move(eventLoop)) {
}

asyncio::fs::File::File(File &&rhs) noexcept
    : mAppend(rhs.mAppend), mFD(std::exchange(rhs.mFD, INVALID_FILE_DESCRIPTOR)), mOffset(rhs.mOffset),
      mEventLoop(std::move(rhs.mEventLoop)) {
}

asyncio::fs::File::~File() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        return;

#ifdef _WIN32
    CloseHandle(reinterpret_cast<HANDLE>(mFD));
#else
    ::close(mFD);
#endif
}

tl::expected<asyncio::fs::File, std::error_code> asyncio::fs::File::from(const FileDescriptor fd, const bool append) {
    auto eventLoop = getEventLoop();
    assert(eventLoop);
    assert(eventLoop->framework());

    TRY(eventLoop->framework()->associate(fd));
    return File{std::move(eventLoop), fd, append};
}

zero::async::coroutine::Task<void, std::error_code> asyncio::fs::File::close() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

#ifdef _WIN32
    if (!CloseHandle(reinterpret_cast<HANDLE>(std::exchange(mFD, INVALID_FILE_DESCRIPTOR))))
        co_return tl::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
#else
    if (::close(std::exchange(mFD, INVALID_FILE_DESCRIPTOR)) != 0)
        co_return tl::unexpected(std::error_code(errno, std::system_category()));
#endif

    co_return tl::expected<void, std::error_code>{};
}

zero::async::coroutine::Task<std::size_t, std::error_code> asyncio::fs::File::read(const std::span<std::byte> data) {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

#ifdef _WIN32
    const auto result = CO_TRY(
        co_await mEventLoop
                 ->framework()
                 ->read(mEventLoop, mFD, mOffset, data)
                 .transformError([](const auto &ec) -> std::error_code {
                     if (ec == std::error_code{ERROR_HANDLE_EOF, std::system_category()})
                         return IO_EOF;

                     return ec;
                 })
    );
#else
    const auto result = CO_TRY(co_await mEventLoop->framework()->read(mEventLoop, mFD, mOffset, data));

    if (*result == 0)
        co_return tl::unexpected(IO_EOF);
#endif

    mOffset += *result;
    co_return *result;
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::fs::File::write(const std::span<const std::byte> data) {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mAppend) {
        CO_TRY(seek(0, END));
    }

    const auto result = CO_TRY(co_await mEventLoop->framework()->write(mEventLoop, mFD, mOffset, data));
    mOffset += *result;
    co_return *result;
}

tl::expected<std::uint64_t, std::error_code> asyncio::fs::File::seek(const std::int64_t offset, const Whence whence) {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    tl::expected<std::uint64_t, std::error_code> result;

    switch (whence) {
    case BEGIN:
        if (offset < 0) {
            result = tl::unexpected(make_error_code(std::errc::invalid_argument));
            break;
        }

        result = mOffset = offset;
        break;

    case CURRENT:
        if (offset < 0 && -offset > mOffset) {
            result = tl::unexpected(make_error_code(std::errc::invalid_argument));
            break;
        }

        result = mOffset = mOffset + offset;
        break;

    case END:
#ifdef _WIN32
        LARGE_INTEGER pos;

        if (!SetFilePointerEx(reinterpret_cast<HANDLE>(mFD), LARGE_INTEGER{.QuadPart = offset}, &pos, FILE_END)) {
            result = tl::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
            break;
        }

        result = mOffset = pos.QuadPart;
        break;
#else
#ifdef __APPLE__
        const off_t pos = lseek(mFD, offset, SEEK_END);
#else
        const off64_t pos = lseek64(mFD, offset, SEEK_END);
#endif

        if (pos == -1) {
            result = tl::unexpected(std::error_code(errno, std::system_category()));
            break;
        }

        result = mOffset = pos;
        break;
#endif
    }

    return result;
}

tl::expected<void, std::error_code> asyncio::fs::File::rewind() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    mOffset = 0;
    return {};
}

tl::expected<std::uint64_t, std::error_code> asyncio::fs::File::length() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    const std::uint64_t current = mOffset;
    const auto pos = TRY(seek(0, END));

    mOffset = current;
    return *pos;
}

tl::expected<std::uint64_t, std::error_code> asyncio::fs::File::position() {
    if (mFD == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    return mOffset;
}

asyncio::FileDescriptor asyncio::fs::File::fd() {
    return mFD;
}

tl::expected<asyncio::fs::File, std::error_code> asyncio::fs::open(const std::filesystem::path &path) {
    return open(path, O_RDONLY);
}

tl::expected<asyncio::fs::File, std::error_code> asyncio::fs::open(const std::filesystem::path &path, int flags) {
    const bool append = flags & O_APPEND;

#ifdef _WIN32
    if (flags != O_RDONLY && !(flags & O_WRONLY) && !(flags & O_RDWR))
        return tl::unexpected(make_error_code(std::errc::invalid_argument));

    const bool read = flags == O_RDONLY || flags & O_RDWR;
    const bool write = flags & O_WRONLY || flags & O_RDWR;

    DWORD access;

    if (read && !write && !append)
        access = GENERIC_READ;
    else if (!read && write && !append)
        access = GENERIC_WRITE;
    else if (read && write && !append)
        access = GENERIC_READ | GENERIC_WRITE;
    else if (!read && append)
        access = FILE_GENERIC_WRITE & ~FILE_WRITE_DATA;
    else if (read && write && append)
        access = GENERIC_READ | FILE_GENERIC_WRITE & ~FILE_WRITE_DATA;
    else
        return tl::unexpected(make_error_code(std::errc::invalid_argument));

    const bool create = flags & O_CREAT;
    const bool truncate = flags & O_TRUNC;
    const bool createNew = flags & O_EXCL;

    DWORD disposition;

    if (!create && !truncate && !createNew)
        disposition = OPEN_EXISTING;
    else if (create && !truncate && !createNew)
        disposition = OPEN_ALWAYS;
    else if (!create && truncate && !createNew)
        disposition = TRUNCATE_EXISTING;
    else if (create && truncate && !createNew)
        disposition = CREATE_ALWAYS;
    else if (create && createNew)
        disposition = CREATE_NEW;
    else
        return tl::unexpected(make_error_code(std::errc::invalid_argument));

    const auto handle = CreateFileA(
        path.string().c_str(),
        access,
        FILE_SHARE_READ,
        nullptr,
        disposition,
        FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE)
        return tl::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));

    const auto fd = reinterpret_cast<FileDescriptor>(handle);
#else
    const int fd = ::open(path.string().c_str(), flags, 0644);

    if (fd < 0)
        return tl::unexpected(std::error_code(errno, std::system_category()));
#endif

    auto eventLoop = getEventLoop();
    assert(eventLoop);
    assert(eventLoop->framework());

    if (const auto result = eventLoop->framework()->associate(fd); !result) {
#ifdef _WIN32
        CloseHandle(handle);
#else
        ::close(fd);
#endif
        return tl::unexpected(result.error());
    }

    return File{std::move(eventLoop), fd, append};
}

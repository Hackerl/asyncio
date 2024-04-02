#include <asyncio/fs/iocp.h>
#include <asyncio/promise.h>
#include <zero/defer.h>
#include <cassert>

struct Request {
    OVERLAPPED overlapped{};
    asyncio::Promise<std::size_t, std::error_code> promise;
};

asyncio::fs::IOCP::IOCP(const HANDLE handle) : mHandle(handle), mThread(&IOCP::dispatch, handle) {
}

asyncio::fs::IOCP::IOCP(IOCP &&rhs) noexcept
    : mHandle(std::exchange(rhs.mHandle, nullptr)), mThread(std::move(rhs.mThread)) {
    assert(mHandle);
    assert(mThread.joinable());
}

asyncio::fs::IOCP &asyncio::fs::IOCP::operator=(IOCP &&rhs) noexcept {
    assert(rhs.mHandle);
    assert(rhs.mThread.joinable());

    mHandle = std::exchange(rhs.mHandle, nullptr);
    mThread = std::move(rhs.mThread);

    return *this;
}

asyncio::fs::IOCP::~IOCP() {
    if (!mHandle)
        return;

    PostQueuedCompletionStatus(mHandle, 0, -1, nullptr);
    mThread.join();
    CloseHandle(mHandle);
}

tl::expected<asyncio::fs::IOCP, std::error_code> asyncio::fs::IOCP::make() {
    const auto handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);

    if (!handle)
        return tl::unexpected<std::error_code>(static_cast<int>(GetLastError()), std::system_category());

    return IOCP{handle};
}

void asyncio::fs::IOCP::dispatch(const HANDLE handle) {
    while (true) {
        DWORD n;
        ULONG_PTR key;
        LPOVERLAPPED overlapped;

        const int result = GetQueuedCompletionStatus(handle, &n, &key, &overlapped, INFINITE);

        if (key == -1)
            break;

        if (!overlapped && !result)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category());

        const auto request = reinterpret_cast<Request *>(overlapped);

        if (!result) {
            request->promise.reject(static_cast<int>(GetLastError()), std::system_category());
            continue;
        }

        request->promise.resolve(n);
    }
}

tl::expected<void, std::error_code> asyncio::fs::IOCP::associate(const FileDescriptor fd) {
    if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(fd), mHandle, 0, 0))
        return tl::unexpected<std::error_code>(static_cast<int>(GetLastError()), std::system_category());

    return {};
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::fs::IOCP::read(
    std::shared_ptr<EventLoop> eventLoop,
    const FileDescriptor fd,
    const std::uint64_t offset,
    std::span<std::byte> data
) {
    Request request = {
        .overlapped = {
            .Offset = static_cast<DWORD>(offset),
            .OffsetHigh = static_cast<DWORD>(offset >> 32),
        },
        .promise = asyncio::Promise<std::size_t, std::error_code>{std::move(eventLoop)}
    };

    DWORD n;
    const auto handle = reinterpret_cast<HANDLE>(fd);

    if (!ReadFile(handle, data.data(), data.size(), &n, &request.overlapped) && GetLastError() != ERROR_IO_PENDING)
        co_return tl::unexpected<std::error_code>(static_cast<int>(GetLastError()), std::system_category());

    co_return co_await zero::async::coroutine::Cancellable{
        request.promise.getFuture(),
        [&]() -> tl::expected<void, std::error_code> {
            if (!CancelIoEx(handle, &request.overlapped))
                return tl::unexpected<std::error_code>(static_cast<int>(GetLastError()), std::system_category());

            return {};
        }
    };
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::fs::IOCP::write(
    std::shared_ptr<EventLoop> eventLoop,
    const FileDescriptor fd,
    const std::uint64_t offset,
    const std::span<const std::byte> data
) {
    Request request = {
        .overlapped = {
            .Offset = static_cast<DWORD>(offset),
            .OffsetHigh = static_cast<DWORD>(offset >> 32),
        },
        .promise = asyncio::Promise<std::size_t, std::error_code>{std::move(eventLoop)}
    };

    DWORD n;
    const auto handle = reinterpret_cast<HANDLE>(fd);

    if (!WriteFile(handle, data.data(), data.size(), &n, &request.overlapped) && GetLastError() != ERROR_IO_PENDING)
        co_return tl::unexpected<std::error_code>(static_cast<int>(GetLastError()), std::system_category());

    co_return co_await zero::async::coroutine::Cancellable{
        request.promise.getFuture(),
        [&]() -> tl::expected<void, std::error_code> {
            if (!CancelIoEx(handle, &request.overlapped))
                return tl::unexpected<std::error_code>(static_cast<int>(GetLastError()), std::system_category());

            return {};
        }
    };
}

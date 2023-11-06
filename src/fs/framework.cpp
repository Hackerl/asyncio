#include <asyncio/fs/framework.h>
#include <cassert>

#ifdef _WIN32
#include <zero/defer.h>
#elif __unix__ || __APPLE__
#include <csignal>
#endif

#if __unix__ || __APPLE__
asyncio::fs::PosixAIO::PosixAIO(EventLoop *eventLoop, std::unique_ptr<event, decltype(event_free) *> event)
        : mEventLoop(eventLoop), mEvent(std::move(event)) {
    assert(mEventLoop);
    auto e = mEvent.get();

    evsignal_assign(
            e,
            event_get_base(e),
            SIGIO,
            [](evutil_socket_t, short, void *arg) {
                static_cast<PosixAIO *>(arg)->onSignal();
            },
            this
    );

    evsignal_add(e, nullptr);
}

asyncio::fs::PosixAIO::PosixAIO(asyncio::fs::PosixAIO &&rhs) noexcept
        : mEventLoop(rhs.mEventLoop), mEvent(std::move(rhs.mEvent)) {
    assert(mPending.empty());
    auto e = mEvent.get();

    evsignal_del(e);
    evsignal_assign(
            e,
            event_get_base(e),
            event_get_signal(e),
            event_get_callback(e),
            this
    );

    evsignal_add(e, nullptr);
}

asyncio::fs::PosixAIO::~PosixAIO() {
    assert(mPending.empty());

    if (!mEvent)
        return;

    evsignal_del(mEvent.get());
}

tl::expected<void, std::error_code> asyncio::fs::PosixAIO::associate(FileDescriptor fd) {
    return {};
}

zero::async::coroutine::Task<size_t, std::error_code>
asyncio::fs::PosixAIO::read(FileDescriptor fd, std::streamoff offset, std::span<std::byte> data) {
    aiocb cb = {};

    cb.aio_fildes = fd;
    cb.aio_offset = offset;
    cb.aio_buf = data.data();
    cb.aio_nbytes = data.size();

    cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    cb.aio_sigevent.sigev_signo = SIGIO;

    zero::async::promise::Promise<size_t, std::error_code> promise;
    auto pending = std::pair{&cb, promise};

    mPending.push_back(pending);

    if (aio_read(&cb) < 0) {
        mPending.remove(pending);
        co_return tl::unexpected(std::error_code(errno, std::system_category()));
    }

    co_return co_await zero::async::coroutine::Cancellable{
            promise,
            [=, this]() mutable -> tl::expected<void, std::error_code> {
                int result = aio_cancel(fd, pending.first);

                if (result == -1)
                    return tl::unexpected(std::error_code(errno, std::system_category()));

                if (result == AIO_ALLDONE)
                    return tl::unexpected(make_error_code(std::errc::operation_not_supported));

                if (result == AIO_NOTCANCELED)
                    return tl::unexpected(make_error_code(std::errc::operation_in_progress));

                mPending.remove(pending);
                pending.second.reject(make_error_code(std::errc::operation_canceled));

                return {};
            }
    };
}

zero::async::coroutine::Task<size_t, std::error_code>
asyncio::fs::PosixAIO::write(FileDescriptor fd, std::streamoff offset, std::span<const std::byte> data) {
    aiocb cb = {};

    cb.aio_fildes = fd;
    cb.aio_offset = offset;
    cb.aio_buf = (void *) data.data();
    cb.aio_nbytes = data.size();

    cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    cb.aio_sigevent.sigev_signo = SIGIO;

    zero::async::promise::Promise<size_t, std::error_code> promise;
    auto pending = std::pair{&cb, promise};

    mPending.push_back(pending);

    if (aio_write(&cb) < 0) {
        mPending.remove(pending);
        co_return tl::unexpected(std::error_code(errno, std::system_category()));
    }

    co_return co_await zero::async::coroutine::Cancellable{
            promise,
            [=, this]() mutable -> tl::expected<void, std::error_code> {
                int result = aio_cancel(fd, pending.first);

                if (result == -1)
                    return tl::unexpected(std::error_code(errno, std::system_category()));

                if (result == AIO_ALLDONE)
                    return tl::unexpected(make_error_code(std::errc::operation_not_supported));

                if (result == AIO_NOTCANCELED)
                    return tl::unexpected(make_error_code(std::errc::operation_in_progress));

                mPending.remove(pending);
                pending.second.reject(make_error_code(std::errc::operation_canceled));

                return {};
            }
    };
}

void asyncio::fs::PosixAIO::onSignal() {
    auto it = mPending.begin();

    while (it != mPending.end()) {
        assert(it->second.status() == zero::async::promise::State::PENDING);
        int error = aio_error(it->first);

        if (error > 0) {
            if (error == EINPROGRESS) {
                it++;
                continue;
            }

            mEventLoop->post([error = errno, promise = std::move(it->second)]() mutable {
                promise.reject(std::error_code(error, std::system_category()));
            });

            it = mPending.erase(it);
            continue;
        }

        ssize_t size = aio_return(it->first);

        if (size == -1) {
            mEventLoop->post([error = errno, promise = std::move(it->second)]() mutable {
                promise.reject(std::error_code(error, std::system_category()));
            });

            it = mPending.erase(it);
            continue;
        }

        mEventLoop->post([=, promise = std::move(it->second)]() mutable {
            promise.resolve(size);
        });

        it = mPending.erase(it);
    }
}

tl::expected<asyncio::fs::PosixAIO, std::error_code> asyncio::fs::makePosixAIO(EventLoop *eventLoop) {
    event *e = evsignal_new(eventLoop->base(), -1, nullptr, nullptr);

    if (!e)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return PosixAIO{eventLoop, {e, event_free}};
}
#endif

#ifdef _WIN32
struct IORequest {
    OVERLAPPED overlapped{};
    zero::async::promise::Promise<size_t, std::error_code> promise;
};

asyncio::fs::IOCP::IOCP(EventLoop *eventLoop, HANDLE handle)
        : mHandle(handle), mEventLoop(eventLoop), mThread(&IOCP::dispatch, this) {
    assert(mEventLoop);
}

asyncio::fs::IOCP::IOCP(asyncio::fs::IOCP &&rhs) noexcept: mEventLoop(rhs.mEventLoop) {
    assert(mHandle);
    assert(rhs.mThread.joinable());

    PostQueuedCompletionStatus(rhs.mHandle, 0, -1, nullptr);
    rhs.mThread.join();

    mHandle = std::exchange(rhs.mHandle, nullptr);
    mThread = std::thread(&IOCP::dispatch, this);
}

asyncio::fs::IOCP::~IOCP() {
    if (!mHandle)
        return;

    PostQueuedCompletionStatus(mHandle, 0, -1, nullptr);
    mThread.join();
    CloseHandle(mHandle);
}

tl::expected<void, std::error_code> asyncio::fs::IOCP::associate(FileDescriptor fd) {
    if (!CreateIoCompletionPort((HANDLE) fd, mHandle, 0, 0))
        return tl::unexpected(std::error_code((int) GetLastError(), std::system_category()));

    return {};
}

zero::async::coroutine::Task<size_t, std::error_code>
asyncio::fs::IOCP::read(FileDescriptor fd, std::streamoff offset, std::span<std::byte> data) {
    IORequest request;

    request.overlapped.Offset = (DWORD) offset;
    request.overlapped.OffsetHigh = (DWORD) (offset >> 32);

    DWORD n;
    auto handle = (HANDLE) fd;

    if (!ReadFile(handle, data.data(), data.size(), &n, &request.overlapped) && GetLastError() != ERROR_IO_PENDING)
        co_return tl::unexpected(std::error_code((int) GetLastError(), std::system_category()));

    co_return co_await zero::async::coroutine::Cancellable{
            request.promise,
            [&]() -> tl::expected<void, std::error_code> {
                if (!CancelIoEx(handle, &request.overlapped))
                    return tl::unexpected(std::error_code((int) GetLastError(), std::system_category()));

                return {};
            }
    };
}

zero::async::coroutine::Task<size_t, std::error_code>
asyncio::fs::IOCP::write(FileDescriptor fd, std::streamoff offset, std::span<const std::byte> data) {
    IORequest request;

    request.overlapped.Offset = (DWORD) offset;
    request.overlapped.OffsetHigh = (DWORD) (offset >> 32);

    DWORD n;
    auto handle = (HANDLE) fd;

    if (!WriteFile(handle, data.data(), data.size(), &n, &request.overlapped) && GetLastError() != ERROR_IO_PENDING)
        co_return tl::unexpected(std::error_code((int) GetLastError(), std::system_category()));

    co_return co_await zero::async::coroutine::Cancellable{
            request.promise,
            [&]() -> tl::expected<void, std::error_code> {
                if (!CancelIoEx(handle, &request.overlapped))
                    return tl::unexpected(std::error_code((int) GetLastError(), std::system_category()));

                return {};
            }
    };
}

void asyncio::fs::IOCP::dispatch() {
    while (true) {
        DWORD n;
        ULONG_PTR key;
        LPOVERLAPPED overlapped;

        int result = GetQueuedCompletionStatus(mHandle, &n, &key, &overlapped, INFINITE);

        if (key == -1)
            break;

        if (!overlapped && !result)
            throw std::system_error((int) GetLastError(), std::system_category());

        auto request = reinterpret_cast<IORequest *>(overlapped);

        if (!result) {
            mEventLoop->post([error = GetLastError(), promise = request->promise]() mutable {
                promise.reject(std::error_code((int) error, std::system_category()));
            });

            continue;
        }

        mEventLoop->post([=, promise = request->promise]() mutable {
            promise.resolve(n);
        });
    }
}

tl::expected<asyncio::fs::IOCP, std::error_code> asyncio::fs::makeIOCP(EventLoop *eventLoop) {
    HANDLE handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);

    if (!handle)
        return tl::unexpected(std::error_code((int) GetLastError(), std::system_category()));

    return IOCP{eventLoop, handle};
}
#endif

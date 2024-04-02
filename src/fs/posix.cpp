#include <asyncio/fs/posix.h>
#include <asyncio/event_loop.h>
#include <csignal>
#include <cassert>

asyncio::fs::PosixAIO::PosixAIO(std::unique_ptr<event, decltype(event_free) *> event) : mEvent(std::move(event)) {
    const auto e = mEvent.get();

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

asyncio::fs::PosixAIO::PosixAIO(PosixAIO &&rhs) noexcept: mEvent(std::move(rhs.mEvent)) {
    assert(mPending.empty());
    const auto e = mEvent.get();

    evsignal_del(e);
    evsignal_assign(e, event_get_base(e), event_get_signal(e), event_get_callback(e), this);
    evsignal_add(e, nullptr);
}

asyncio::fs::PosixAIO::~PosixAIO() {
    assert(mPending.empty());

    if (!mEvent)
        return;

    evsignal_del(mEvent.get());
}

tl::expected<asyncio::fs::PosixAIO, std::error_code> asyncio::fs::PosixAIO::make(event_base *base) {
    event *e = evsignal_new(base, -1, nullptr, nullptr);

    if (!e)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    return PosixAIO{{e, event_free}};
}

void asyncio::fs::PosixAIO::onSignal() {
    auto it = mPending.begin();

    while (it != mPending.end()) {
        assert(!(*it)->promise.isFulfilled());

        if (const int error = aio_error((*it)->cb); error > 0) {
            if (error == EINPROGRESS) {
                ++it;
                continue;
            }

            (*it)->promise.reject(error, std::system_category());
            it = mPending.erase(it);
            continue;
        }

        const ssize_t size = aio_return((*it)->cb);

        if (size == -1) {
            (*it)->promise.reject(errno, std::system_category());
            it = mPending.erase(it);
            continue;
        }

        (*it)->promise.resolve(size);
        it = mPending.erase(it);
    }
}

tl::expected<void, std::error_code> asyncio::fs::PosixAIO::associate(FileDescriptor fd) {
    return {};
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::fs::PosixAIO::read(
    std::shared_ptr<EventLoop> eventLoop,
    const FileDescriptor fd,
    const std::uint64_t offset,
    std::span<std::byte> data
) {
    aiocb cb = {};

    cb.aio_fildes = fd;
    cb.aio_offset = static_cast<off_t>(offset);
    cb.aio_buf = data.data();
    cb.aio_nbytes = data.size();

    cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    cb.aio_sigevent.sigev_signo = SIGIO;

    auto pending = Request{&cb, Promise<std::size_t, std::error_code>{std::move(eventLoop)}};
    mPending.push_back(&pending);

    if (aio_read(&cb) < 0) {
        mPending.remove(&pending);
        co_return tl::unexpected<std::error_code>(errno, std::system_category());
    }

    co_return co_await zero::async::coroutine::Cancellable{
        pending.promise.getFuture(),
        [&, this]() -> tl::expected<void, std::error_code> {
            if (pending.promise.isFulfilled())
                return tl::unexpected(make_error_code(std::errc::operation_not_supported));

            const int result = aio_cancel(fd, pending.cb);

            if (result == -1)
                return tl::unexpected<std::error_code>(errno, std::system_category());

            if (result == AIO_ALLDONE)
                return tl::unexpected(make_error_code(std::errc::operation_not_supported));

            if (result == AIO_NOTCANCELED)
                return tl::unexpected(make_error_code(std::errc::operation_in_progress));

            mPending.remove(&pending);
            pending.promise.reject(make_error_code(std::errc::operation_canceled));

            return {};
        }
    };
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::fs::PosixAIO::write(
    std::shared_ptr<EventLoop> eventLoop,
    const FileDescriptor fd,
    const std::uint64_t offset,
    std::span<const std::byte> data
) {
    aiocb cb = {};

    cb.aio_fildes = fd;
    cb.aio_offset = static_cast<off_t>(offset);
    cb.aio_buf = static_cast<void *>(const_cast<std::byte *>(data.data()));
    cb.aio_nbytes = data.size();

    cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    cb.aio_sigevent.sigev_signo = SIGIO;

    auto pending = Request{&cb, Promise<std::size_t, std::error_code>{std::move(eventLoop)}};
    mPending.push_back(&pending);

    if (aio_write(&cb) < 0) {
        mPending.remove(&pending);
        co_return tl::unexpected<std::error_code>(errno, std::system_category());
    }

    co_return co_await zero::async::coroutine::Cancellable{
        pending.promise.getFuture(),
        [&, this]() -> tl::expected<void, std::error_code> {
            if (pending.promise.isFulfilled())
                return tl::unexpected(make_error_code(std::errc::operation_not_supported));

            const int result = aio_cancel(fd, pending.cb);

            if (result == -1)
                return tl::unexpected<std::error_code>(errno, std::system_category());

            if (result == AIO_ALLDONE)
                return tl::unexpected(make_error_code(std::errc::operation_not_supported));

            if (result == AIO_NOTCANCELED)
                return tl::unexpected(make_error_code(std::errc::operation_in_progress));

            mPending.remove(&pending);
            pending.promise.reject(make_error_code(std::errc::operation_canceled));

            return {};
        }
    };
}

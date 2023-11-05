#include <asyncio/fs/framework.h>
#include <asyncio/event_loop.h>
#include <csignal>

#if __unix__ || __APPLE__
asyncio::fs::PosixAIO::PosixAIO(std::unique_ptr<event, decltype(event_free) *> event) : mEvent(std::move(event)) {
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

asyncio::fs::PosixAIO::PosixAIO(asyncio::fs::PosixAIO &&rhs) noexcept: mEvent(std::move(rhs.mEvent)) {
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

zero::async::coroutine::Task<size_t, std::error_code>
asyncio::fs::PosixAIO::read(FileDescriptor fd, off_t offset, std::span<std::byte> data) {
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

                if (result == AIO_ALLDONE || result == AIO_NOTCANCELED)
                    return tl::unexpected(make_error_code(std::errc::io_error));

                mPending.remove(pending);
                pending.second.reject(make_error_code(std::errc::operation_canceled));

                return {};
            }
    };
}

zero::async::coroutine::Task<size_t, std::error_code>
asyncio::fs::PosixAIO::write(FileDescriptor fd, off_t offset, std::span<const std::byte> data) {
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

                if (result == AIO_ALLDONE || result == AIO_NOTCANCELED)
                    return tl::unexpected(make_error_code(std::errc::io_error));

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

            getEventLoop()->post([promise = std::move(it->second)]() mutable {
                promise.reject(std::error_code(errno, std::system_category()));
            });

            it = mPending.erase(it);
            continue;
        }

        ssize_t size = aio_return(it->first);

        if (size == -1) {
            getEventLoop()->post([promise = std::move(it->second)]() mutable {
                promise.reject(std::error_code(errno, std::system_category()));
            });

            it = mPending.erase(it);
            continue;
        }

        getEventLoop()->post([=, promise = std::move(it->second)]() mutable {
            promise.resolve(size);
        });

        it = mPending.erase(it);
    }
}

tl::expected<asyncio::fs::PosixAIO, std::error_code> asyncio::fs::makePosixAIO() {
    event *e = evsignal_new(getEventLoop()->base(), -1, nullptr, nullptr);

    if (!e)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return PosixAIO{{e, event_free}};
}
#endif

#include <asyncio/promise.h>
#include <asyncio/fs/aio.h>
#include <sys/eventfd.h>
#include <syscall.h>
#include <unistd.h>

int io_setup(const unsigned int nr_events, aio_context_t *ctx_idp) {
    return static_cast<int>(syscall(SYS_io_setup, nr_events, ctx_idp));
}

int io_destroy(const aio_context_t ctx_id) {
    return static_cast<int>(syscall(SYS_io_destroy, ctx_id));
}

int io_getevents(const aio_context_t ctx_id, const long min_nr, const long nr, io_event *events, timespec *timeout) {
    return static_cast<int>(syscall(SYS_io_getevents, ctx_id, min_nr, nr, events, timeout));
}

int io_submit(const aio_context_t ctx_id, const long nr, iocb **iocbpp) {
    return static_cast<int>(syscall(SYS_io_submit, ctx_id, nr, iocbpp));
}

int io_cancel(const aio_context_t ctx_id, iocb *iocb, io_event *result) {
    return static_cast<int>(syscall(SYS_io_cancel, ctx_id, iocb, result));
}

asyncio::fs::AIO::AIO(const aio_context_t context, const int efd, std::unique_ptr<event, decltype(event_free) *> event)
    : mEventFD(efd), mContext(context), mEvent(std::move(event)) {
    const auto e = mEvent.get();

    event_assign(
        e,
        event_get_base(e),
        mEventFD,
        EV_READ | EV_PERSIST,
        [](evutil_socket_t, short, void *arg) {
            static_cast<AIO *>(arg)->onEvent();
        },
        this
    );

    event_add(e, nullptr);
}

asyncio::fs::AIO::AIO(AIO &&rhs) noexcept
    : mEventFD(std::exchange(rhs.mEventFD, -1)),
      mContext(std::exchange(rhs.mContext, 0)),
      mEvent(std::move(rhs.mEvent)) {
    const auto e = mEvent.get();

    event_del(e);
    event_assign(e, event_get_base(e), event_get_fd(e), EV_READ | EV_PERSIST, event_get_callback(e), this);
    event_add(e, nullptr);
}

asyncio::fs::AIO &asyncio::fs::AIO::operator=(AIO &&rhs) noexcept {
    mEventFD = std::exchange(rhs.mEventFD, -1);
    mContext = std::exchange(rhs.mContext, 0);
    mEvent = std::move(rhs.mEvent);

    const auto e = mEvent.get();

    event_del(e);
    event_assign(e, event_get_base(e), event_get_fd(e), EV_READ | EV_PERSIST, event_get_callback(e), this);
    event_add(e, nullptr);

    return *this;
}

asyncio::fs::AIO::~AIO() {
    if (!mEvent)
        return;

    event_del(mEvent.get());
    io_destroy(mContext);
    close(mEventFD);
}

tl::expected<asyncio::fs::AIO, std::error_code> asyncio::fs::AIO::make(event_base *base) {
    aio_context_t ctx = {};

    if (io_setup(128, &ctx) != 0)
        return tl::unexpected<std::error_code>(errno, std::system_category());

    const int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    if (efd < 0) {
        io_destroy(ctx);
        return tl::unexpected<std::error_code>(errno, std::system_category());
    }

    event *e = event_new(base, -1, EV_READ | EV_PERSIST, nullptr, nullptr);

    if (!e) {
        close(efd);
        io_destroy(ctx);
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());
    }

    return AIO{ctx, efd, {e, event_free}};
}

void asyncio::fs::AIO::onEvent() const {
    eventfd_t value;
    eventfd_read(mEventFD, &value);

    io_event events[128] = {};
    const int n = io_getevents(mContext, 1, 128, events, nullptr);
    assert(n > 0);

    for (io_event *event = events; event < events + n; ++event) {
        const auto promise = reinterpret_cast<Promise<std::size_t, std::error_code> *>(event->data);

        if (event->res < 0)
            promise->reject(static_cast<int>(-event->res), std::system_category());
        else
            promise->resolve(event->res);
    }
}

tl::expected<void, std::error_code> asyncio::fs::AIO::associate(const FileDescriptor) {
    return {};
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::fs::AIO::read(
    std::shared_ptr<EventLoop> eventLoop,
    const FileDescriptor fd,
    const std::uint64_t offset,
    const std::span<std::byte> data
) {
    iocb cb = {};
    Promise<std::size_t, std::error_code> promise(std::move(eventLoop));

    cb.aio_data = reinterpret_cast<__u64>(&promise);
    cb.aio_lio_opcode = IOCB_CMD_PREAD;
    cb.aio_fildes = fd;
    cb.aio_buf = reinterpret_cast<__u64>(data.data());
    cb.aio_nbytes = data.size();
    cb.aio_offset = static_cast<__s64>(offset);
    cb.aio_flags |= IOCB_FLAG_RESFD;
    cb.aio_resfd = mEventFD;

    if (iocb *cbs[1] = {&cb}; io_submit(mContext, 1, cbs) != 1)
        co_return tl::unexpected<std::error_code>(errno, std::system_category());

    co_return co_await zero::async::coroutine::Cancellable{
        promise.getFuture(),
        [&]() -> tl::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

            io_event result = {};

            if (io_cancel(mContext, &cb, &result) != 0)
                return tl::unexpected<std::error_code>(errno, std::system_category());

            promise.reject(zero::async::coroutine::Error::CANCELLED);
            return {};
        }
    };
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::fs::AIO::write(
    std::shared_ptr<EventLoop> eventLoop,
    const FileDescriptor fd,
    const std::uint64_t offset,
    const std::span<const std::byte> data
) {
    iocb cb = {};
    Promise<std::size_t, std::error_code> promise(std::move(eventLoop));

    cb.aio_data = reinterpret_cast<__u64>(&promise);
    cb.aio_lio_opcode = IOCB_CMD_PWRITE;
    cb.aio_fildes = fd;
    cb.aio_buf = reinterpret_cast<__u64>(data.data());
    cb.aio_nbytes = data.size();
    cb.aio_offset = static_cast<__s64>(offset);
    cb.aio_flags |= IOCB_FLAG_RESFD;
    cb.aio_resfd = mEventFD;

    if (iocb *cbs[1] = {&cb}; io_submit(mContext, 1, cbs) != 1)
        co_return tl::unexpected<std::error_code>(errno, std::system_category());

    co_return co_await zero::async::coroutine::Cancellable{
        promise.getFuture(),
        [&]() -> tl::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

            io_event result = {};

            if (io_cancel(mContext, &cb, &result) != 0)
                return tl::unexpected<std::error_code>(errno, std::system_category());

            promise.reject(zero::async::coroutine::Error::CANCELLED);
            return {};
        }
    };
}

#include <asyncio/poll.h>
#include <asyncio/error.h>

asyncio::Poll::Poll(uv::Handle<uv_poll_t> poll) : mPoll{std::move(poll)} {
}

asyncio::Poll asyncio::Poll::make(const int fd) {
    auto poll = std::make_unique<uv_poll_t>();

    zero::error::guard(uv::expected([&] {
        return uv_poll_init(getEventLoop()->raw(), poll.get(), fd);
    }));

    return Poll{uv::Handle{std::move(poll)}};
}

#ifdef _WIN32
asyncio::Poll asyncio::Poll::make(const SOCKET socket) {
    auto poll = std::make_unique<uv_poll_t>();

    zero::error::guard(uv::expected([&] {
        return uv_poll_init_socket(getEventLoop()->raw(), poll.get(), socket);
    }));

    return Poll{uv::Handle{std::move(poll)}};
}
#endif

asyncio::FileDescriptor asyncio::Poll::fd() const {
    return zero::error::guard(mPoll.fd());
}

asyncio::task::Task<int, std::error_code> asyncio::Poll::on(const int events) {
    Promise<int, std::error_code> promise;
    mPoll->data = &promise;

    co_await error::guard(uv::expected([&] {
        return uv_poll_start(
            mPoll.raw(),
            events,
            [](auto *handle, const int status, const int e) {
                zero::error::guard(uv::expected([&] {
                    return uv_poll_stop(handle);
                }));

                const auto p = static_cast<Promise<int, std::error_code> *>(handle->data);

                if (status < 0) {
                    p->reject(static_cast<uv::Error>(status));
                    return;
                }

                p->resolve(e);
            }
        );
    }));

    co_return co_await task::Cancellable{
        promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return std::unexpected{task::Error::CancellationTooLate};

            zero::error::guard(uv::expected([&] {
                return uv_poll_stop(mPoll.raw());
            }));

            promise.reject(task::Error::Cancelled);
            return {};
        }
    };
}

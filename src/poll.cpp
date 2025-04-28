#include <asyncio/poll.h>

asyncio::Poll::Poll(uv::Handle<uv_poll_t> poll) : mPoll{std::move(poll)} {

}

std::expected<asyncio::Poll, std::error_code> asyncio::Poll::make(const int fd) {
    auto poll = std::make_unique<uv_poll_t>();

    EXPECT(uv::expected([&] {
        return uv_poll_init(getEventLoop()->raw(), poll.get(), fd);
    }));

    return Poll{uv::Handle{std::move(poll)}};
}

#ifdef _WIN32
std::expected<asyncio::Poll, std::error_code> asyncio::Poll::make(const SOCKET socket) {
    auto poll = std::make_unique<uv_poll_t>();

    EXPECT(uv::expected([&] {
        return uv_poll_init_socket(getEventLoop()->raw(), poll.get(), socket);
    }));

    return Poll{uv::Handle{std::move(poll)}};
}
#endif

asyncio::FileDescriptor asyncio::Poll::fd() const {
    const auto fd = mPoll.fd();
    assert(fd);
    return *fd;
}

asyncio::task::Task<int, std::error_code> asyncio::Poll::on(const int events) {
    Promise<int, std::error_code> promise;
    mPoll->data = &promise;

    CO_EXPECT(uv::expected([&] {
        return uv_poll_start(
            mPoll.raw(),
            events,
            [](auto *handle, const int status, const int e) {
                uv_poll_stop(handle);
                const auto p = static_cast<Promise<int, std::error_code> *>(handle->data);

                if (status < 0) {
                    p->reject(static_cast<uv::Error>(status));
                    return;
                }

                p->resolve(e);
            }
        );
    }));

    co_return co_await task::CancellableFuture{
        promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return std::unexpected{task::Error::WILL_BE_DONE};

            uv_poll_stop(mPoll.raw());
            promise.reject(task::Error::CANCELLED);
            return {};
        }
    };
}

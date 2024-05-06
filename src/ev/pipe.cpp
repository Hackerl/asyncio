#include <asyncio/ev/pipe.h>
#include <asyncio/event_loop.h>
#include <cassert>

asyncio::ev::PairedBuffer::PairedBuffer(
    std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev,
    const std::size_t capacity
) : Buffer(std::move(bev), capacity) {
}

asyncio::ev::PairedBuffer::~PairedBuffer() {
    if (!mBev || mClosed)
        return;

    bufferevent_flush(mBev.get(), EV_WRITE, BEV_FINISHED);
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::PairedBuffer::close() {
    if (!mBev)
        co_return tl::unexpected(IOError::BAD_FILE_DESCRIPTOR);

    assert(!mPromises[READ_INDEX]);
    assert(!mPromises[WRITE_INDEX]);

    bufferevent_flush(mBev.get(), EV_WRITE, BEV_FINISHED);
    mBev.reset();

    co_return {};
}

tl::expected<std::array<asyncio::ev::PairedBuffer, 2>, std::error_code> asyncio::ev::pipe(const std::size_t capacity) {
    bufferevent *pair[2];

    if (bufferevent_pair_new(getEventLoop()->base(), BEV_OPT_DEFER_CALLBACKS, pair) < 0)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    return std::array{
        PairedBuffer{{pair[0], bufferevent_free}, capacity},
        PairedBuffer{{pair[1], bufferevent_free}, capacity}
    };
}

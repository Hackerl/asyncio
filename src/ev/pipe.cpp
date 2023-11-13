#include <asyncio/ev/pipe.h>
#include <asyncio/event_loop.h>

asyncio::ev::PairedBuffer::PairedBuffer(bufferevent *bev, size_t capacity, std::shared_ptr<std::error_code> ec)
        : Buffer(bev, capacity), mErrorCode(std::move(ec)) {

}

asyncio::ev::PairedBuffer::~PairedBuffer() {
    if (!mBev || mClosed)
        return;

    bufferevent_flush(mBev.get(), EV_WRITE, BEV_FINISHED);
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::PairedBuffer::close() {
    bufferevent_flush(mBev.get(), EV_WRITE, BEV_FINISHED);
    return Buffer::close();
}

tl::expected<void, std::error_code> asyncio::ev::PairedBuffer::throws(const std::error_code &ec) {
    if (!mBev)
        return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mClosed)
        return tl::unexpected(Error::IO_EOF);

    *mErrorCode = ec;

    if (bufferevent *buffer = bufferevent_pair_get_partner(mBev.get()))
        bufferevent_trigger_event(buffer, BEV_EVENT_ERROR, BEV_OPT_DEFER_CALLBACKS);

    bufferevent_trigger_event(mBev.get(), BEV_EVENT_ERROR, BEV_OPT_DEFER_CALLBACKS);
    mClosed = true;

    return {};
}

std::error_code asyncio::ev::PairedBuffer::getError() {
    return *mErrorCode;
}

tl::expected<std::array<asyncio::ev::PairedBuffer, 2>, std::error_code> asyncio::ev::pipe(size_t capacity) {
    bufferevent *pair[2];
    auto base = getEventLoop()->base();

    if (bufferevent_pair_new(base, BEV_OPT_DEFER_CALLBACKS, pair) < 0)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    /*
     * The data will be transmitted to the peer immediately,
     * so the read operation of the peer will return immediately without event callback,
     * and a delayed read event will be triggered when the event loop is running.
     * */
    evbuffer_defer_callbacks(bufferevent_get_output(pair[0]), base);
    evbuffer_defer_callbacks(bufferevent_get_output(pair[1]), base);

    std::shared_ptr<std::error_code> ec = std::make_shared<std::error_code>();

    return std::array{PairedBuffer(pair[0], capacity, ec), PairedBuffer(pair[1], capacity, ec)};
}

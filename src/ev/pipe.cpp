#include <asyncio/ev/pipe.h>
#include <asyncio/event_loop.h>

asyncio::ev::PairedBuffer::PairedBuffer(bufferevent *bev, std::shared_ptr<std::error_code> ec)
        : Buffer(bev), mErrorCode(std::move(ec)) {

}

asyncio::ev::PairedBuffer::~PairedBuffer() {
    if (mClosed)
        return;

    bufferevent_flush(mBev, EV_WRITE, BEV_FINISHED);
}

tl::expected<void, std::error_code> asyncio::ev::PairedBuffer::close() {
    bufferevent_flush(mBev, EV_WRITE, BEV_FINISHED);
    return Buffer::close();
}

std::error_code asyncio::ev::PairedBuffer::getError() {
    return *mErrorCode;
}

void asyncio::ev::PairedBuffer::throws(const std::error_code &ec) {
    *mErrorCode = ec;

    if (bufferevent *buffer = bufferevent_pair_get_partner(mBev))
        bufferevent_trigger_event(buffer, BEV_EVENT_ERROR, BEV_OPT_DEFER_CALLBACKS);

    bufferevent_trigger_event(mBev, BEV_EVENT_ERROR, 0);
}

std::array<std::shared_ptr<asyncio::ev::IPairedBuffer>, 2> asyncio::ev::pipe() {
    bufferevent *pair[2];
    auto base = getEventLoop()->base();

    if (bufferevent_pair_new(base, BEV_OPT_DEFER_CALLBACKS, pair) < 0)
        return {nullptr, nullptr};

    std::shared_ptr<std::error_code> ec = std::make_shared<std::error_code>();

    std::array<std::shared_ptr<asyncio::ev::IPairedBuffer>, 2> buffers = {
            std::make_shared<PairedBuffer>(pair[0], ec),
            std::make_shared<PairedBuffer>(pair[1], ec)
    };

    /*
     * The data will be transmitted to the peer immediately,
     * so the read operation of the peer will return immediately without event callback,
     * and a delayed read event will be triggered when the event loop is running.
     * */
    evbuffer_defer_callbacks(bufferevent_get_output(pair[0]), base);
    evbuffer_defer_callbacks(bufferevent_get_output(pair[1]), base);

    return buffers;
}

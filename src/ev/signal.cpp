#include <asyncio/ev/signal.h>

asyncio::ev::Signal::Signal(std::unique_ptr<event, decltype(event_free) *> event, const std::size_t capacity)
    : mChannel(asyncio::channel<int>(capacity)), mEvent(std::move(event)) {
    const auto e = mEvent.get();

    evsignal_assign(
        e,
        event_get_base(e),
        event_get_signal(e),
        [](const evutil_socket_t fd, short, void *arg) {
            static_cast<Signal *>(arg)->mChannel.first.trySend(fd);
        },
        this
    );

    evsignal_add(e, nullptr);
}

asyncio::ev::Signal::Signal(Signal &&rhs) noexcept : mChannel(std::move(rhs.mChannel)), mEvent(std::move(rhs.mEvent)) {
    const auto e = mEvent.get();

    evsignal_del(e);
    evsignal_assign(e, event_get_base(e), event_get_signal(e), event_get_callback(e), this);
    evsignal_add(e, nullptr);
}

asyncio::ev::Signal &asyncio::ev::Signal::operator=(Signal &&rhs) noexcept {
    mChannel = std::move(rhs.mChannel);
    mEvent = std::move(rhs.mEvent);

    const auto e = mEvent.get();

    evsignal_del(e);
    evsignal_assign(e, event_get_base(e), event_get_signal(e), event_get_callback(e), this);
    evsignal_add(e, nullptr);

    return *this;
}

asyncio::ev::Signal::~Signal() {
    if (!mEvent)
        return;

    evsignal_del(mEvent.get());
}

tl::expected<asyncio::ev::Signal, std::error_code>
asyncio::ev::Signal::make(const int sig, const std::size_t capacity) {
    event *e = evsignal_new(getEventLoop()->base(), sig, nullptr, nullptr);

    if (!e)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    return Signal{{e, event_free}, capacity};
}

int asyncio::ev::Signal::sig() const {
    return event_get_signal(mEvent.get());
}

zero::async::coroutine::Task<int, std::error_code> asyncio::ev::Signal::on() {
    co_return co_await mChannel.second.receive();
}

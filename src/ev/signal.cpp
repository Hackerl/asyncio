#include <asyncio/ev/signal.h>

asyncio::ev::Signal::Signal(event *e, const std::size_t capacity)
    : mChannel(std::make_unique<Channel<int>>(capacity)), mEvent(e, event_free) {
    evsignal_assign(
        e,
        event_get_base(e),
        event_get_signal(e),
        [](const evutil_socket_t fd, short, void *arg) {
            static_cast<Signal *>(arg)->mChannel->trySend(fd);
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

asyncio::ev::Signal::~Signal() {
    if (!mEvent)
        return;

    evsignal_del(mEvent.get());
}

int asyncio::ev::Signal::sig() const {
    return event_get_signal(mEvent.get());
}

zero::async::coroutine::Task<int, std::error_code> asyncio::ev::Signal::on() const {
    co_return co_await mChannel->receive();
}

tl::expected<asyncio::ev::Signal, std::error_code> asyncio::ev::makeSignal(const int sig, const std::size_t capacity) {
    event *e = evsignal_new(getEventLoop()->base(), sig, nullptr, nullptr);

    if (!e)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return Signal{e, capacity};
}

#include <asyncio/ev/event.h>
#include <asyncio/event_loop.h>

asyncio::ev::Event::Event(int fd) {
    mEvent = event_new(
            getEventLoop()->base(),
            fd,
            0,
            [](evutil_socket_t fd, short what, void *arg) {
                auto promise = std::move(static_cast<Event *>(arg)->mPromise);
                promise->resolve(what);
            },
            this
    );
}

asyncio::ev::Event::~Event() {
    event_free(mEvent);
}

evutil_socket_t asyncio::ev::Event::fd() {
    return event_get_fd(mEvent);
}

bool asyncio::ev::Event::cancel() {
    if (!pending())
        return false;

    event_del(mEvent);

    auto p = std::move(mPromise);
    p->reject(std::make_error_code(std::errc::operation_canceled));

    return true;
}

bool asyncio::ev::Event::pending() {
    return mPromise.operator bool();
}

void asyncio::ev::Event::trigger(short events) {
    event_active(mEvent, events, 0);
}

zero::async::coroutine::Task<short, std::error_code> asyncio::ev::Event::on(
        short events,
        std::optional<std::chrono::milliseconds> timeout
) {
    if (mPromise)
        co_return tl::unexpected(std::make_error_code(std::errc::operation_in_progress));

    co_return co_await zero::async::promise::chain<short, std::error_code>([&](const auto &promise) {
        mPromise = std::make_unique<zero::async::promise::Promise<short, std::error_code>>(promise);
        mEvent->ev_events = events;

        if (!timeout) {
            event_add(mEvent, nullptr);
            return;
        }

        timeval tv = {
                (time_t) (timeout->count() / 1000),
                (suseconds_t) ((timeout->count() % 1000) * 1000)
        };

        event_add(mEvent, &tv);
    });
}

#include <asyncio/ev/event.h>
#include <asyncio/event_loop.h>

asyncio::ev::Event::Event(std::unique_ptr<event, void (*)(event *)> event) : Notifier(std::move(event)) {

}

evutil_socket_t asyncio::ev::Event::fd() {
    return event_get_fd(mEvent.get());
}

void asyncio::ev::Event::trigger(short events) {
    event_active(mEvent.get(), events, 0);
}

zero::async::coroutine::Task<short, std::error_code>
asyncio::ev::Event::on(std::optional<std::chrono::milliseconds> timeout) {
    if (pending())
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    co_return co_await zero::async::promise::chain<short, std::error_code>([&](const auto &promise) {
        context() = promise;

        if (!timeout) {
            event_add(mEvent.get(), nullptr);
            return;
        }

        timeval tv = {
                (decltype(timeval::tv_sec)) (timeout->count() / 1000),
                (decltype(timeval::tv_usec)) ((timeout->count() % 1000) * 1000)
        };

        event_add(mEvent.get(), &tv);
    });
}

tl::expected<asyncio::ev::Event, std::error_code> asyncio::ev::makeEvent(evutil_socket_t fd, short events) {
    auto context = new Event::Context();

    event *e = event_new(
            getEventLoop()->base(),
            fd,
            events,
            [](evutil_socket_t fd, short what, void *arg) {
                std::exchange(*static_cast<Event::Context *>(arg), std::nullopt)->resolve(what);
            },
            context
    );

    if (!e) {
        delete context;
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }

    return Event{
            std::unique_ptr<event, void (*)(event *)>(
                    e,
                    [](event *event) {
                        delete static_cast<Event::Context *>(event_get_callback_arg(event));
                        event_free(event);
                    }
            )
    };
}

#include <asyncio/ev/event.h>
#include <asyncio/event_loop.h>

asyncio::ev::Event::Event(event *e) : Notifier(e) {
}

asyncio::FileDescriptor asyncio::ev::Event::fd() const {
    return event_get_fd(mEvent.get());
}

void asyncio::ev::Event::trigger(const short events) const {
    event_active(mEvent.get(), events, 0);
}

zero::async::coroutine::Task<short, std::error_code>
asyncio::ev::Event::on(const std::optional<std::chrono::milliseconds> timeout) {
    if (pending())
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    co_return co_await zero::async::coroutine::Cancellable{
        zero::async::promise::chain<short, std::error_code>([&](const auto &promise) {
            context() = promise;

            if (!timeout) {
                event_add(mEvent.get(), nullptr);
                return;
            }

            const timeval tv = {
                static_cast<decltype(timeval::tv_sec)>(timeout->count() / 1000),
                static_cast<decltype(timeval::tv_usec)>(timeout->count() % 1000 * 1000)
            };

            event_add(mEvent.get(), &tv);
        }),
        [this]() -> tl::expected<void, std::error_code> {
            event_del(mEvent.get());
            std::exchange(context(), std::nullopt)->reject(make_error_code(std::errc::operation_canceled));
            return {};
        }
    };
}

tl::expected<asyncio::ev::Event, std::error_code> asyncio::ev::makeEvent(const FileDescriptor fd, const short events) {
    const auto context = new Event::Context();

    event *e = event_new(
        getEventLoop()->base(),
        fd,
        events,
        [](evutil_socket_t, short what, void *arg) {
            std::exchange(*static_cast<Event::Context *>(arg), std::nullopt)->resolve(what);
        },
        context
    );

    if (!e) {
        delete context;
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }

    return Event{e};
}

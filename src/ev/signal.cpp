#include <asyncio/ev/signal.h>
#include <asyncio/event_loop.h>

asyncio::ev::Signal::Signal(std::unique_ptr<event, void (*)(event *)> event) : Notifier(std::move(event)) {

}

int asyncio::ev::Signal::sig() {
    return event_get_signal(mEvent.get());
}

zero::async::coroutine::Task<void, std::error_code>
asyncio::ev::Signal::on() {
    if (pending())
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    co_return co_await zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
        context() = promise;
        evsignal_add(mEvent.get(), nullptr);
    });
}

tl::expected<asyncio::ev::Signal, std::error_code> asyncio::ev::makeSignal(int sig) {
    auto context = new Signal::Context();

    event *e = evsignal_new(
            getEventLoop()->base(),
            sig,
            [](evutil_socket_t fd, short what, void *arg) {
                std::exchange(*static_cast<Event::Context *>(arg), std::nullopt)->resolve();
            },
            context
    );

    if (!e) {
        delete context;
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }

    return Signal{
            std::unique_ptr<event, void (*)(event *)>(
                    e,
                    [](event *event) {
                        delete static_cast<Signal::Context *>(event_get_callback_arg(event));
                        event_free(event);
                    }
            )
    };
}

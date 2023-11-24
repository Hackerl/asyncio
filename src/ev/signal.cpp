#include <asyncio/ev/signal.h>
#include <asyncio/event_loop.h>

asyncio::ev::Signal::Signal(event *e) : Notifier(e) {
}

int asyncio::ev::Signal::sig() const {
    return event_get_signal(mEvent.get());
}

zero::async::coroutine::Task<void, std::error_code>
asyncio::ev::Signal::on() {
    if (pending())
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    co_return co_await zero::async::coroutine::Cancellable{
        zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
            context() = promise;
            evsignal_add(mEvent.get(), nullptr);
        }),
        [this]() -> tl::expected<void, std::error_code> {
            evsignal_del(mEvent.get());
            std::exchange(context(), std::nullopt)->reject(make_error_code(std::errc::operation_canceled));
            return {};
        }
    };
}

tl::expected<asyncio::ev::Signal, std::error_code> asyncio::ev::makeSignal(const int sig) {
    const auto context = new Signal::Context();

    event *e = evsignal_new(
        getEventLoop()->base(),
        sig,
        [](evutil_socket_t, short, void *arg) {
        std::exchange(*static_cast<Event::Context *>(arg), std::nullopt)->resolve();
        },
        context
    );

    if (!e) {
        delete context;
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }

    return Signal{e};
}

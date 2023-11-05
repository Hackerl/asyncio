#include <asyncio/ev/timer.h>
#include <asyncio/event_loop.h>

asyncio::ev::Timer::Timer(event *e) : Notifier(e) {

}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Timer::after(std::chrono::milliseconds delay) {
    if (pending())
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    co_return co_await zero::async::coroutine::Cancellable{
            zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
                context() = promise;

                timeval tv = {
                        (decltype(timeval::tv_sec)) (delay.count() / 1000),
                        (decltype(timeval::tv_usec)) ((delay.count() % 1000) * 1000)
                };

                evtimer_add(mEvent.get(), &tv);
            }),
            [this]() -> tl::expected<void, std::error_code> {
                evtimer_del(mEvent.get());
                std::exchange(context(), std::nullopt)->reject(make_error_code(std::errc::operation_canceled));
                return {};
            }
    };
}

tl::expected<asyncio::ev::Timer, std::error_code> asyncio::ev::makeTimer() {
    auto context = new Timer::Context();

    event *e = evtimer_new(
            getEventLoop()->base(),
            [](evutil_socket_t, short, void *arg) {
                std::exchange(*static_cast<Event::Context *>(arg), std::nullopt)->resolve();
            },
            context
    );

    if (!e) {
        delete context;
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }

    return Timer{e};
}

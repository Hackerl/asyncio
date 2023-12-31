#include <asyncio/ev/timer.h>
#include <asyncio/event_loop.h>
#include <cassert>

asyncio::ev::Timer::Timer(event *e) : mEvent(e, event_free) {
    evtimer_assign(
        e,
        event_get_base(e),
        [](evutil_socket_t, short, void *arg) {
            std::exchange(static_cast<Timer *>(arg)->mPromise, std::nullopt)->resolve();
        },
        this
    );
}

asyncio::ev::Timer::Timer(Timer &&rhs) noexcept: mEvent(std::move(rhs.mEvent)) {
    assert(!rhs.mPromise);
    const auto e = mEvent.get();
    evtimer_assign(e, event_get_base(e), event_get_callback(e), this);
}

asyncio::ev::Timer::~Timer() {
    assert(!mEvent || !mPromise);
}

bool asyncio::ev::Timer::cancel() {
    if (!pending())
        return false;

    evtimer_del(mEvent.get());
    std::exchange(mPromise, std::nullopt)->reject(make_error_code(std::errc::operation_canceled));

    return true;
}

bool asyncio::ev::Timer::pending() const {
    return mPromise.has_value();
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Timer::after(const std::chrono::milliseconds delay) {
    if (mPromise)
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    co_return co_await zero::async::coroutine::Cancellable{
        zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
            mPromise = promise;

            const timeval tv = {
                static_cast<decltype(timeval::tv_sec)>(delay.count() / 1000),
                static_cast<decltype(timeval::tv_usec)>(delay.count() % 1000 * 1000)
            };

            evtimer_add(mEvent.get(), &tv);
        }),
        [this]() -> tl::expected<void, std::error_code> {
            evtimer_del(mEvent.get());
            std::exchange(mPromise, std::nullopt)->reject(make_error_code(std::errc::operation_canceled));
            return {};
        }
    };
}

tl::expected<asyncio::ev::Timer, std::error_code> asyncio::ev::makeTimer() {
    event *e = evtimer_new(getEventLoop()->base(), nullptr, nullptr);

    if (!e)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return Timer{e};
}

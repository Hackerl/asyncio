#include <asyncio/ev/timer.h>
#include <asyncio/event_loop.h>
#include <cassert>

asyncio::ev::Timer::Timer(std::unique_ptr<event, decltype(event_free) *> event) : mEvent(std::move(event)) {
    const auto e = mEvent.get();

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

asyncio::ev::Timer &asyncio::ev::Timer::operator=(Timer &&rhs) noexcept {
    assert(!rhs.mPromise);

    mEvent = std::move(rhs.mEvent);
    const auto e = mEvent.get();
    evtimer_assign(e, event_get_base(e), event_get_callback(e), this);

    return *this;
}

asyncio::ev::Timer::~Timer() {
    assert(!mEvent || !mPromise);
}

tl::expected<asyncio::ev::Timer, std::error_code> asyncio::ev::Timer::make() {
    event *e = evtimer_new(getEventLoop()->base(), nullptr, nullptr);

    if (!e)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    return Timer{{e, event_free}};
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Timer::after(const std::chrono::milliseconds delay) {
    if (mPromise)
        co_return tl::unexpected(IOError::DEVICE_OR_RESOURCE_BUSY);

    co_return co_await zero::async::coroutine::Cancellable{
        zero::async::promise::chain<void, std::error_code>([&](auto promise) {
            mPromise.emplace(std::move(promise));

            const timeval tv = {
                static_cast<decltype(timeval::tv_sec)>(delay.count() / 1000),
                static_cast<decltype(timeval::tv_usec)>(delay.count() % 1000 * 1000)
            };

            evtimer_add(mEvent.get(), &tv);
        }),
        [this]() -> tl::expected<void, std::error_code> {
            if (!mPromise)
                return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

            evtimer_del(mEvent.get());
            std::exchange(mPromise, std::nullopt)->reject(zero::async::coroutine::Error::CANCELLED);
            return {};
        }
    };
}

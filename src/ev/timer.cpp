#include <asyncio/ev/timer.h>
#include <asyncio/event_loop.h>

asyncio::ev::Timer::Timer() {
    mEvent = evtimer_new(
            getEventLoop()->base(),
            [](evutil_socket_t fd, short what, void *arg) {
                auto promise = std::move(static_cast<Timer *>(arg)->mPromise);
                promise->resolve();
            },
            this
    );
}

asyncio::ev::Timer::~Timer() {
    event_free(mEvent);
}

bool asyncio::ev::Timer::cancel() {
    if (!pending())
        return false;

    event_del(mEvent);

    auto p = std::move(mPromise);
    p->reject(std::make_error_code(std::errc::operation_canceled));

    return true;
}

bool asyncio::ev::Timer::pending() {
    return mPromise.operator bool();
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Timer::setTimeout(std::chrono::milliseconds delay) {
    if (mPromise)
        co_return tl::unexpected(std::make_error_code(std::errc::operation_in_progress));

    co_return co_await zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
        mPromise = std::make_unique<zero::async::promise::Promise<void, std::error_code>>(promise);

        timeval tv = {
                (time_t) (delay.count() / 1000),
                (suseconds_t) ((delay.count() % 1000) * 1000)
        };

        evtimer_add(mEvent, &tv);
    });
}

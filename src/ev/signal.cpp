#include <asyncio/ev/signal.h>
#include <asyncio/event_loop.h>

asyncio::ev::Signal::Signal(int sig) {
    mEvent = evsignal_new(
            getEventLoop()->base(),
            sig,
            [](evutil_socket_t fd, short event, void *arg) {
                auto promise = std::move(static_cast<Signal *>(arg)->mPromise);
                promise->resolve();
            },
            this
    );
}

asyncio::ev::Signal::~Signal() {
    event_free(mEvent);
}

int asyncio::ev::Signal::sig() {
    return event_get_signal(mEvent);
}

bool asyncio::ev::Signal::cancel() {
    if (!pending())
        return false;

    event_del(mEvent);

    auto p = std::move(mPromise);
    p->reject(make_error_code(std::errc::operation_canceled));

    return true;
}

bool asyncio::ev::Signal::pending() {
    return mPromise.operator bool();
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Signal::on() {
    if (mPromise)
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    co_return co_await zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
        mPromise = std::make_unique<zero::async::promise::Promise<void, std::error_code>>(promise);
        evsignal_add(mEvent, nullptr);
    });
}

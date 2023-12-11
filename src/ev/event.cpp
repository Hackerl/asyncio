#include <asyncio/ev/event.h>
#include <asyncio/event_loop.h>
#include <cassert>

asyncio::ev::Event::Event(event *e) : mEvent(e, event_free) {
    event_assign(
        e,
        event_get_base(e),
        event_get_fd(e),
        event_get_events(e),
        [](evutil_socket_t, short what, void *arg) {
            std::exchange(static_cast<Event *>(arg)->mPromise, std::nullopt)->resolve(what);
        },
        this
    );
}

asyncio::ev::Event::Event(Event &&rhs) noexcept: mEvent(std::move(rhs.mEvent)) {
    assert(!rhs.mPromise);
    const auto e = mEvent.get();

    event_assign(
        e,
        event_get_base(e),
        event_get_fd(e),
        event_get_events(e),
        event_get_callback(e),
        this
    );
}

asyncio::ev::Event::~Event() {
    assert(!mEvent || !mPromise);
}

asyncio::FileDescriptor asyncio::ev::Event::fd() const {
    return event_get_fd(mEvent.get());
}

bool asyncio::ev::Event::cancel() {
    if (!pending())
        return false;

    event_del(mEvent.get());
    std::exchange(mPromise, std::nullopt)->reject(make_error_code(std::errc::operation_canceled));

    return true;
}

bool asyncio::ev::Event::pending() const {
    return mPromise.has_value();
}

zero::async::coroutine::Task<short, std::error_code>
asyncio::ev::Event::on(const std::optional<std::chrono::milliseconds> timeout) {
    if (mPromise)
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    co_return co_await zero::async::coroutine::Cancellable{
        zero::async::promise::chain<short, std::error_code>([&](const auto &promise) {
            mPromise = promise;

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
            std::exchange(mPromise, std::nullopt)->reject(make_error_code(std::errc::operation_canceled));
            return {};
        }
    };
}

tl::expected<asyncio::ev::Event, std::error_code> asyncio::ev::makeEvent(const FileDescriptor fd, const short events) {
    event *e = event_new(getEventLoop()->base(), fd, events, nullptr, nullptr);

    if (!e)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return Event{e};
}

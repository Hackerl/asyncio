#include <asyncio/ev/event.h>
#include <asyncio/event_loop.h>
#include <cassert>

asyncio::ev::Event::Event(std::unique_ptr<event, decltype(event_free) *> event) : mEvent(std::move(event)) {
    const auto e = mEvent.get();

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

asyncio::ev::Event &asyncio::ev::Event::operator=(Event &&rhs) noexcept {
    assert(!rhs.mPromise);
    mEvent = std::move(rhs.mEvent);
    const auto e = mEvent.get();

    event_assign(
        e,
        event_get_base(e),
        event_get_fd(e),
        event_get_events(e),
        event_get_callback(e),
        this
    );

    return *this;
}

asyncio::ev::Event::~Event() {
    assert(!mEvent || !mPromise);
}

tl::expected<asyncio::ev::Event, std::error_code> asyncio::ev::Event::make(const FileDescriptor fd, const short events) {
    event *e = event_new(getEventLoop()->base(), fd, events, nullptr, nullptr);

    if (!e)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    return Event{{e, event_free}};
}

asyncio::FileDescriptor asyncio::ev::Event::fd() const {
    return event_get_fd(mEvent.get());
}

bool asyncio::ev::Event::pending() const {
    return mPromise.operator bool();
}

zero::async::coroutine::Task<short, std::error_code>
asyncio::ev::Event::on(const std::optional<std::chrono::milliseconds> timeout) {
    if (mPromise)
        co_return tl::unexpected(IOError::DEVICE_OR_RESOURCE_BUSY);

    co_return co_await zero::async::coroutine::Cancellable{
        zero::async::promise::chain<short, std::error_code>([&](auto promise) {
            mPromise.emplace(std::move(promise));

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
            if (!mPromise)
                return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

            event_del(mEvent.get());
            std::exchange(mPromise, std::nullopt)->reject(zero::async::coroutine::Error::CANCELLED);
            return {};
        }
    };
}

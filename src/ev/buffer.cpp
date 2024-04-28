#include <asyncio/ev/buffer.h>
#include <asyncio/event_loop.h>
#include <optional>
#include <cassert>

asyncio::ev::Buffer::Buffer(std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev, const std::size_t capacity)
    : mClosed(false), mCapacity(capacity), mBev(std::move(bev)) {
    assert(mCapacity > 0);

    bufferevent_setcb(
        mBev.get(),
        nullptr,
        nullptr,
        [](bufferevent *, const short what, void *arg) {
            static_cast<Buffer *>(arg)->onEvent(what);
        },
        this
    );

    bufferevent_enable(mBev.get(), EV_READ | EV_WRITE);
    bufferevent_setwatermark(mBev.get(), EV_READ, 0, mCapacity);
    bufferevent_setwatermark(mBev.get(), EV_WRITE, mCapacity, 0);
}

asyncio::ev::Buffer::Buffer(Buffer &&rhs) noexcept
    : mClosed(rhs.mClosed), mCapacity(rhs.mCapacity), mLastError(rhs.mLastError), mBev(std::move(rhs.mBev)) {
    assert(!rhs.mPromises[READ_INDEX]);
    assert(!rhs.mPromises[WRITE_INDEX]);

    bufferevent_event_cb ecb;

    bufferevent_getcb(mBev.get(), nullptr, nullptr, &ecb, nullptr);
    bufferevent_setcb(mBev.get(), nullptr, nullptr, ecb, this);
}

asyncio::ev::Buffer &asyncio::ev::Buffer::operator=(Buffer &&rhs) noexcept {
    assert(!rhs.mPromises[READ_INDEX]);
    assert(!rhs.mPromises[WRITE_INDEX]);

    mClosed = rhs.mClosed;
    mCapacity = rhs.mCapacity;
    mLastError = rhs.mLastError;
    mBev = std::move(rhs.mBev);

    bufferevent_data_cb rcb, wcb;
    bufferevent_event_cb ecb;

    bufferevent_getcb(mBev.get(), &rcb, &wcb, &ecb, nullptr);
    bufferevent_setcb(mBev.get(), rcb, wcb, ecb, this);

    return *this;
}

asyncio::ev::Buffer::~Buffer() {
    assert(!mPromises[READ_INDEX]);
    assert(!mPromises[WRITE_INDEX]);
}

tl::expected<asyncio::ev::Buffer, std::error_code>
asyncio::ev::Buffer::make(const FileDescriptor fd, const std::size_t capacity, const bool own) {
    bufferevent *bev = bufferevent_socket_new(getEventLoop()->base(), fd, own ? BEV_OPT_CLOSE_ON_FREE : 0);

    if (!bev)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    return Buffer{{bev, bufferevent_free}, capacity};
}

void asyncio::ev::Buffer::resize(const std::size_t capacity) {
    assert(capacity > 0);
    assert(!mPromises[READ_INDEX]);
    assert(!mPromises[WRITE_INDEX]);

    mCapacity = capacity;
    bufferevent_setwatermark(mBev.get(), EV_READ, 0, mCapacity);
    bufferevent_setwatermark(mBev.get(), EV_WRITE, mCapacity, 0);
}

void asyncio::ev::Buffer::onEvent(const short what) {
    if (what & BEV_EVENT_EOF) {
        onClose(UNEXPECTED_EOF);
    }
    else if (what & BEV_EVENT_ERROR) {
        onClose(getError());
    }
    else if (what & BEV_EVENT_TIMEOUT) {
        if (what & BEV_EVENT_READING) {
            auto promise = std::exchange(mPromises[READ_INDEX], std::nullopt);

            if (!promise)
                return;

            promise->reject(TIMED_OUT);
        }
        else {
            auto promise = std::exchange(mPromises[WRITE_INDEX], std::nullopt);

            if (!promise)
                return;

            promise->reject(TIMED_OUT);
        }
    }
}


void asyncio::ev::Buffer::onClose(const std::error_code &ec) {
    mClosed = true;
    mLastError = ec;

    for (auto &promise: std::exchange(mPromises, {})) {
        if (!promise)
            continue;

        promise->reject(ec);
    }
}

void asyncio::ev::Buffer::controlReadEvent(const bool enable) {
    bufferevent_data_cb wcb;
    bufferevent_event_cb ecb;

    bufferevent_getcb(mBev.get(), nullptr, &wcb, &ecb, nullptr);

    if (!enable) {
        bufferevent_setcb(mBev.get(), nullptr, wcb, ecb, this);
        return;
    }

    bufferevent_setcb(
        mBev.get(),
        [](bufferevent *, void *arg) {
            auto p = std::exchange(static_cast<Buffer *>(arg)->mPromises[READ_INDEX], std::nullopt);

            if (!p)
                return;

            p->resolve();
        },
        wcb,
        ecb,
        this
    );
}

void asyncio::ev::Buffer::controlWriteEvent(const bool enable) {
    bufferevent_data_cb rcb;
    bufferevent_event_cb ecb;

    bufferevent_getcb(mBev.get(), &rcb, nullptr, &ecb, nullptr);

    if (!enable) {
        bufferevent_setcb(mBev.get(), rcb, nullptr, ecb, this);
        return;
    }

    bufferevent_setcb(
        mBev.get(),
        rcb,
        [](bufferevent *, void *arg) {
            auto p = std::exchange(static_cast<Buffer *>(arg)->mPromises[WRITE_INDEX], std::nullopt);

            if (!p)
                return;

            p->resolve();
        },
        ecb,
        this
    );
}

std::error_code asyncio::ev::Buffer::getError() const {
    return {EVUTIL_SOCKET_ERROR(), std::system_category()};
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::close() {
    if (!mBev)
        co_return tl::unexpected(BAD_FILE_DESCRIPTOR);

    assert(!mPromises[READ_INDEX]);
    assert(!mPromises[WRITE_INDEX]);

    if (!mClosed)
        co_await flush();

    mBev.reset();
    co_return tl::expected<void, std::error_code>{};
}

zero::async::coroutine::Task<std::size_t, std::error_code> asyncio::ev::Buffer::read(const std::span<std::byte> data) {
    if (!mBev)
        co_return tl::unexpected(BAD_FILE_DESCRIPTOR);

    if (mPromises[READ_INDEX])
        co_return tl::unexpected(DEVICE_OR_RESOURCE_BUSY);

    evbuffer *input = bufferevent_get_input(mBev.get());
    std::size_t length = evbuffer_get_length(input);

    if (length > 0) {
        std::size_t n = (std::min)(length, data.size());
        evbuffer_remove(input, data.data(), n);
        co_return n;
    }

    if (mClosed) {
        if (mLastError == UNEXPECTED_EOF)
            co_return 0;

        co_return tl::unexpected(mLastError);
    }

    if (const auto result = co_await zero::async::coroutine::Cancellable{
        zero::async::promise::chain<void, std::error_code>([this](auto promise) {
            mPromises[READ_INDEX].emplace(std::move(promise));

            controlReadEvent(true);
            bufferevent_enable(mBev.get(), EV_READ);
        }).finally([this] {
            controlReadEvent(false);
        }),
        [this]() -> tl::expected<void, std::error_code> {
            if (!mPromises[READ_INDEX])
                return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

            std::exchange(mPromises[READ_INDEX], std::nullopt)->reject(
                zero::async::coroutine::Error::CANCELLED
            );
            return {};
        }
    }; !result) {
        if (result.error() == UNEXPECTED_EOF)
            co_return 0;

        co_return tl::unexpected(result.error());
    }

    length = evbuffer_get_length(input);
    const std::size_t n = (std::min)(length, data.size());

    evbuffer_remove(input, data.data(), n);
    co_return n;
}

std::size_t asyncio::ev::Buffer::available() const {
    if (!mBev)
        return -1;

    return evbuffer_get_length(bufferevent_get_input(mBev.get()));
}

zero::async::coroutine::Task<std::string, std::error_code> asyncio::ev::Buffer::readLine() {
    if (!mBev)
        co_return tl::unexpected(BAD_FILE_DESCRIPTOR);

    if (mPromises[READ_INDEX])
        co_return tl::unexpected(DEVICE_OR_RESOURCE_BUSY);

    evbuffer *input = bufferevent_get_input(mBev.get());
    tl::expected<std::string, std::error_code> result;

    while (true) {
        if (char *ptr = evbuffer_readln(input, nullptr, EVBUFFER_EOL_CRLF)) {
            result = std::unique_ptr<char, decltype(free) *>(ptr, free).get();
            break;
        }

        if (mClosed) {
            result = tl::unexpected(mLastError);
            break;
        }

        if (const auto res = co_await zero::async::coroutine::Cancellable{
            zero::async::promise::chain<void, std::error_code>([this](auto promise) {
                mPromises[READ_INDEX].emplace(std::move(promise));

                bufferevent_setwatermark(mBev.get(), EV_READ, 0, 0);
                controlReadEvent(true);
                bufferevent_enable(mBev.get(), EV_READ);
            }).finally([this] {
                controlReadEvent(false);
                bufferevent_setwatermark(mBev.get(), EV_READ, 0, mCapacity);
            }),
            [this]() -> tl::expected<void, std::error_code> {
                if (!mPromises[READ_INDEX])
                    return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

                std::exchange(mPromises[READ_INDEX], std::nullopt)->reject(
                    zero::async::coroutine::Error::CANCELLED
                );
                return {};
            }
        }; !res) {
            result = tl::unexpected(res.error());
            break;
        }
    }

    co_return result;
}

zero::async::coroutine::Task<std::vector<std::byte>, std::error_code>
asyncio::ev::Buffer::readUntil(const std::byte byte) {
    co_return tl::unexpected(FUNCTION_NOT_SUPPORTED);
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::peek(const std::span<std::byte> data) {
    if (!mBev)
        co_return tl::unexpected(BAD_FILE_DESCRIPTOR);

    if (mPromises[READ_INDEX])
        co_return tl::unexpected(DEVICE_OR_RESOURCE_BUSY);

    if (data.size() > mCapacity)
        co_return tl::unexpected(INVALID_ARGUMENT);

    evbuffer *input = bufferevent_get_input(mBev.get());

    if (evbuffer_get_length(input) >= data.size()) {
        evbuffer_copyout(input, data.data(), data.size());
        co_return tl::expected<void, std::error_code>{};
    }

    if (mClosed)
        co_return tl::unexpected(mLastError);

    if (const auto result = co_await zero::async::coroutine::Cancellable{
        zero::async::promise::chain<void, std::error_code>([&](auto promise) {
            mPromises[READ_INDEX].emplace(std::move(promise));
            bufferevent_setwatermark(mBev.get(), EV_READ, data.size(), mCapacity);
            controlReadEvent(true);
            bufferevent_enable(mBev.get(), EV_READ);
        }).finally([this] {
            controlReadEvent(false);
            bufferevent_setwatermark(mBev.get(), EV_READ, 0, mCapacity);
        }),
        [this]() -> tl::expected<void, std::error_code> {
            if (!mPromises[READ_INDEX])
                return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

            std::exchange(mPromises[READ_INDEX], std::nullopt)->reject(
                zero::async::coroutine::Error::CANCELLED
            );
            return {};
        }
    }; !result)
        co_return tl::unexpected(result.error());

    evbuffer_copyout(input, data.data(), data.size());
    co_return tl::expected<void, std::error_code>{};
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::ev::Buffer::write(const std::span<const std::byte> data) {
    if (!mBev)
        co_return tl::unexpected(BAD_FILE_DESCRIPTOR);

    if (mClosed)
        co_return tl::unexpected(BROKEN_PIPE);

    if (mPromises[WRITE_INDEX])
        co_return tl::unexpected(DEVICE_OR_RESOURCE_BUSY);

    evbuffer *output = bufferevent_get_output(mBev.get());
    tl::expected<std::size_t, std::error_code> result;

    while (*result < data.size()) {
        const std::size_t length = evbuffer_get_length(output);

        if (length >= mCapacity) {
            if (const auto res = co_await zero::async::coroutine::Cancellable{
                zero::async::promise::chain<void, std::error_code>([this](auto promise) {
                    mPromises[WRITE_INDEX].emplace(std::move(promise));
                    controlWriteEvent(true);
                    bufferevent_enable(mBev.get(), EV_WRITE);
                }).finally([this] {
                    controlWriteEvent(false);
                }),
                [this]() -> tl::expected<void, std::error_code> {
                    if (!mPromises[WRITE_INDEX])
                        return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

                    std::exchange(mPromises[WRITE_INDEX], std::nullopt)->reject(
                        zero::async::coroutine::Error::CANCELLED
                    );
                    return {};
                }
            }; !res) {
                if (*result > 0)
                    break;

                result = tl::unexpected(res.error());
                break;
            }

            continue;
        }

        const std::size_t n = (std::min)(mCapacity - length, data.size() - *result);
        evbuffer_add(output, data.data() + *result, n);

        *result += n;
    }

    co_return result;
}

std::size_t asyncio::ev::Buffer::pending() const {
    if (!mBev)
        return -1;

    return evbuffer_get_length(bufferevent_get_output(mBev.get()));
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::flush() {
    if (!mBev)
        co_return tl::unexpected(BAD_FILE_DESCRIPTOR);

    if (mClosed)
        co_return tl::unexpected(BROKEN_PIPE);

    if (mPromises[WRITE_INDEX])
        co_return tl::unexpected(DEVICE_OR_RESOURCE_BUSY);

    if (evbuffer_get_length(bufferevent_get_output(mBev.get())) == 0)
        co_return tl::expected<void, std::error_code>{};

    co_return co_await zero::async::coroutine::Cancellable{
        zero::async::promise::chain<void, std::error_code>([this](auto promise) {
            mPromises[WRITE_INDEX].emplace(std::move(promise));
            bufferevent_setwatermark(mBev.get(), EV_WRITE, 0, 0);
            controlWriteEvent(true);
            bufferevent_enable(mBev.get(), EV_WRITE);
        }).finally([this] {
            controlWriteEvent(false);
            bufferevent_setwatermark(mBev.get(), EV_WRITE, mCapacity, 0);
        }),
        [this]() -> tl::expected<void, std::error_code> {
            if (!mPromises[WRITE_INDEX])
                return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

            std::exchange(mPromises[WRITE_INDEX], std::nullopt)->reject(
                zero::async::coroutine::Error::CANCELLED
            );
            return {};
        }
    };
}

asyncio::FileDescriptor asyncio::ev::Buffer::fd() const {
    if (!mBev)
        return INVALID_FILE_DESCRIPTOR;

    return bufferevent_getfd(mBev.get());
}

tl::expected<void, std::error_code> asyncio::ev::Buffer::setTimeout(const std::chrono::milliseconds timeout) {
    return setTimeout(timeout, timeout);
}

tl::expected<void, std::error_code>
asyncio::ev::Buffer::setTimeout(
    const std::chrono::milliseconds readTimeout,
    const std::chrono::milliseconds writeTimeout
) {
    if (!mBev)
        return tl::unexpected(BAD_FILE_DESCRIPTOR);

    std::optional<timeval> rtv, wtv;

    if (readTimeout != std::chrono::milliseconds::zero())
        rtv = {
            static_cast<decltype(timeval::tv_sec)>(readTimeout.count() / 1000),
            static_cast<decltype(timeval::tv_usec)>(readTimeout.count() % 1000 * 1000)
        };

    if (writeTimeout != std::chrono::milliseconds::zero())
        wtv = {
            static_cast<decltype(timeval::tv_sec)>(writeTimeout.count() / 1000),
            static_cast<decltype(timeval::tv_usec)>(writeTimeout.count() % 1000 * 1000)
        };

    bufferevent_set_timeouts(
        mBev.get(),
        rtv ? &*rtv : nullptr,
        wtv ? &*wtv : nullptr
    );

    return {};
}

std::size_t asyncio::ev::Buffer::capacity() const {
    return mCapacity;
}

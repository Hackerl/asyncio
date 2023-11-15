#include <asyncio/ev/buffer.h>
#include <asyncio/event_loop.h>
#include <optional>
#include <cassert>

constexpr auto READ_INDEX = 0;
constexpr auto WRITE_INDEX = 1;
constexpr auto DEFAULT_BUFFER_TIMEOUT = 120;

asyncio::ev::Buffer::Buffer(bufferevent *bev, size_t capacity) : Buffer({bev, bufferevent_free}, capacity) {

}

asyncio::ev::Buffer::Buffer(std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev, size_t capacity)
        : mCapacity(capacity), mBev(std::move(bev)), mClosed(false) {
    bufferevent_setcb(
            mBev.get(),
            [](bufferevent *, void *arg) {
                static_cast<Buffer *>(arg)->onBufferRead();
            },
            [](bufferevent *, void *arg) {
                static_cast<Buffer *>(arg)->onBufferWrite();
            },
            [](bufferevent *, short what, void *arg) {
                static_cast<Buffer *>(arg)->onBufferEvent(what);
            },
            this
    );

    bufferevent_enable(mBev.get(), EV_READ | EV_WRITE);
    bufferevent_setwatermark(mBev.get(), EV_READ, 0, mCapacity);
    bufferevent_setwatermark(mBev.get(), EV_WRITE, mCapacity, 0);

    timeval tv = {.tv_sec = DEFAULT_BUFFER_TIMEOUT};
    bufferevent_set_timeouts(mBev.get(), &tv, &tv);
}

asyncio::ev::Buffer::Buffer(asyncio::ev::Buffer &&rhs) noexcept
        : mCapacity(rhs.mCapacity), mBev(std::move(rhs.mBev)), mClosed(rhs.mClosed), mLastError(rhs.mLastError) {
    assert(!rhs.mPromises[READ_INDEX]);
    assert(!rhs.mPromises[WRITE_INDEX]);

    bufferevent_data_cb rcb, wcb;
    bufferevent_event_cb ecb;

    bufferevent_getcb(mBev.get(), &rcb, &wcb, &ecb, nullptr);
    bufferevent_setcb(mBev.get(), rcb, wcb, ecb, this);
}

asyncio::ev::Buffer::~Buffer() {
    assert(!mPromises[READ_INDEX]);
    assert(!mPromises[WRITE_INDEX]);
}

void asyncio::ev::Buffer::resize(size_t capacity) {
    assert(!mPromises[READ_INDEX]);
    assert(!mPromises[WRITE_INDEX]);

    mCapacity = capacity;
    bufferevent_setwatermark(mBev.get(), EV_READ, 0, mCapacity);
    bufferevent_setwatermark(mBev.get(), EV_WRITE, mCapacity, 0);
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::close() {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mClosed) {
        mBev.reset();
        co_return tl::unexpected(mLastError);
    }

    co_await flush();

    onClose(Error::IO_EOF);
    mBev.reset();

    co_return tl::expected<void, std::error_code>{};
}

zero::async::coroutine::Task<size_t, std::error_code> asyncio::ev::Buffer::read(std::span<std::byte> data) {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromises[READ_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    evbuffer *input = bufferevent_get_input(mBev.get());
    size_t length = evbuffer_get_length(input);

    if (length > 0) {
        size_t n = (std::min)(length, data.size());
        evbuffer_remove(input, data.data(), n);
        co_return n;
    }

    if (mClosed)
        co_return tl::unexpected(mLastError);

    auto result = co_await zero::async::coroutine::Cancellable{
            zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
                mPromises[READ_INDEX] = promise;
                bufferevent_enable(mBev.get(), EV_READ);
            }),
            [this]() -> tl::expected<void, std::error_code> {
                std::exchange(mPromises[READ_INDEX], std::nullopt)->reject(
                        make_error_code(std::errc::operation_canceled)
                );
                return {};
            }
    };

    if (!result)
        co_return tl::unexpected(result.error());

    length = evbuffer_get_length(input);
    size_t n = (std::min)(length, data.size());

    evbuffer_remove(input, data.data(), n);
    co_return n;
}

size_t asyncio::ev::Buffer::available() {
    if (!mBev)
        return -1;

    return evbuffer_get_length(bufferevent_get_input(mBev.get()));
}

zero::async::coroutine::Task<std::string, std::error_code> asyncio::ev::Buffer::readLine() {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromises[READ_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    evbuffer *input = bufferevent_get_input(mBev.get());
    tl::expected<std::string, std::error_code> result;

    while (true) {
        char *ptr = evbuffer_readln(input, nullptr, EVBUFFER_EOL_CRLF);

        if (ptr) {
            result = std::unique_ptr<char, decltype(free) *>(ptr, free).get();
            break;
        }

        if (mClosed) {
            result = tl::unexpected<std::error_code>(mLastError);
            break;
        }

        auto res = co_await zero::async::coroutine::Cancellable{
                zero::async::promise::chain<void, std::error_code>([this](const auto &promise) {
                    mPromises[READ_INDEX] = promise;
                    bufferevent_enable(mBev.get(), EV_READ);
                    bufferevent_setwatermark(mBev.get(), EV_READ, 0, 0);
                }).finally([this]() {
                    bufferevent_setwatermark(mBev.get(), EV_READ, 0, mCapacity);
                }),
                [this]() -> tl::expected<void, std::error_code> {
                    std::exchange(mPromises[READ_INDEX], std::nullopt)->reject(
                            make_error_code(std::errc::operation_canceled)
                    );
                    return {};
                }
        };

        if (!res) {
            result = tl::unexpected(res.error());
            break;
        }
    }

    co_return result;
}

zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> asyncio::ev::Buffer::readUntil(std::byte byte) {
    co_return tl::unexpected(make_error_code(std::errc::function_not_supported));
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::peek(std::span<std::byte> data) {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromises[READ_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    if (data.size() > mCapacity)
        co_return tl::unexpected(make_error_code(std::errc::invalid_argument));

    evbuffer *input = bufferevent_get_input(mBev.get());

    if (evbuffer_get_length(input) >= data.size()) {
        evbuffer_copyout(input, data.data(), data.size());
        co_return tl::expected<void, std::error_code>{};
    }

    if (mClosed)
        co_return tl::unexpected(mLastError);

    auto result = co_await zero::async::coroutine::Cancellable{
            zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
                mPromises[READ_INDEX] = promise;
                bufferevent_setwatermark(mBev.get(), EV_READ, data.size(), mCapacity);
                bufferevent_enable(mBev.get(), EV_READ);
            }).finally([this]() {
                bufferevent_setwatermark(mBev.get(), EV_READ, 0, mCapacity);
            }),
            [this]() -> tl::expected<void, std::error_code> {
                std::exchange(mPromises[READ_INDEX], std::nullopt)->reject(
                        make_error_code(std::errc::operation_canceled)
                );
                return {};
            }
    };

    if (!result)
        co_return tl::unexpected(result.error());

    evbuffer_copyout(input, data.data(), data.size());
    co_return tl::expected<void, std::error_code>{};
}

zero::async::coroutine::Task<size_t, std::error_code> asyncio::ev::Buffer::write(std::span<const std::byte> data) {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromises[WRITE_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    if (mClosed)
        co_return tl::unexpected(mLastError);

    evbuffer *output = bufferevent_get_output(mBev.get());
    tl::expected<size_t, std::error_code> result;

    while (*result < data.size()) {
        size_t length = evbuffer_get_length(output);

        if (length >= mCapacity) {
            auto res = co_await zero::async::coroutine::Cancellable{
                    zero::async::promise::chain<void, std::error_code>([=, this](const auto &promise) {
                        mPromises[WRITE_INDEX] = promise;
                        bufferevent_enable(mBev.get(), EV_WRITE);
                    }),
                    [this]() -> tl::expected<void, std::error_code> {
                        if (!mPromises[WRITE_INDEX])
                            return tl::unexpected(make_error_code(std::errc::operation_not_supported));

                        std::exchange(mPromises[WRITE_INDEX], std::nullopt)->reject(
                                make_error_code(std::errc::operation_canceled)
                        );
                        return {};
                    }
            };

            if (!res) {
                if (*result > 0)
                    break;

                result = tl::unexpected(res.error());
                break;
            }

            continue;
        }

        size_t n = (std::min)(mCapacity - length, data.size() - *result);
        evbuffer_add(output, data.data() + *result, n);

        *result += n;
    }

    co_return result;
}

size_t asyncio::ev::Buffer::pending() {
    if (!mBev)
        return -1;

    return evbuffer_get_length(bufferevent_get_output(mBev.get()));
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::flush() {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromises[WRITE_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    if (mClosed)
        co_return tl::unexpected(mLastError);

    evbuffer *output = bufferevent_get_output(mBev.get());

    if (evbuffer_get_length(output) == 0)
        co_return tl::expected<void, std::error_code>{};

    co_return co_await zero::async::coroutine::Cancellable{
            zero::async::promise::chain<void, std::error_code>([this](const auto &promise) {
                mPromises[WRITE_INDEX] = promise;
                bufferevent_setwatermark(mBev.get(), EV_WRITE, 0, 0);
            }).finally([this]() {
                bufferevent_setwatermark(mBev.get(), EV_WRITE, mCapacity, 0);
            }),
            [this]() -> tl::expected<void, std::error_code> {
                if (!mPromises[WRITE_INDEX])
                    return tl::unexpected(make_error_code(std::errc::operation_not_supported));

                std::exchange(mPromises[WRITE_INDEX], std::nullopt)->reject(
                        make_error_code(std::errc::operation_canceled)
                );
                return {};
            }
    };
}

asyncio::FileDescriptor asyncio::ev::Buffer::fd() {
    if (!mBev)
        return INVALID_FILE_DESCRIPTOR;

    return bufferevent_getfd(mBev.get());
}

void asyncio::ev::Buffer::setTimeout(std::chrono::milliseconds timeout) {
    setTimeout(timeout, timeout);
}

void asyncio::ev::Buffer::setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) {
    if (!mBev)
        return;

    std::optional<timeval> rtv, wtv;

    if (readTimeout != std::chrono::milliseconds::zero())
        rtv = {
                (decltype(timeval::tv_sec)) (readTimeout.count() / 1000),
                (decltype(timeval::tv_usec)) ((readTimeout.count() % 1000) * 1000)
        };

    if (writeTimeout != std::chrono::milliseconds::zero())
        wtv = {
                (decltype(timeval::tv_sec)) (writeTimeout.count() / 1000),
                (decltype(timeval::tv_usec)) ((writeTimeout.count() % 1000) * 1000)
        };

    bufferevent_set_timeouts(
            mBev.get(),
            rtv ? &*rtv : nullptr,
            wtv ? &*wtv : nullptr
    );
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

void asyncio::ev::Buffer::onBufferRead() {
    auto promise = std::exchange(mPromises[READ_INDEX], std::nullopt);

    if (!promise)
        return;

    promise->resolve();
}

void asyncio::ev::Buffer::onBufferWrite() {
    auto promise = std::exchange(mPromises[WRITE_INDEX], std::nullopt);

    if (!promise)
        return;

    promise->resolve();
}

void asyncio::ev::Buffer::onBufferEvent(short what) {
    if (what & BEV_EVENT_EOF) {
        onClose(Error::IO_EOF);
    } else if (what & BEV_EVENT_ERROR) {
        onClose(getError());
    } else if (what & BEV_EVENT_TIMEOUT) {
        if (what & BEV_EVENT_READING) {
            auto promise = std::exchange(mPromises[READ_INDEX], std::nullopt);

            if (!promise)
                return;

            promise->reject(make_error_code(std::errc::timed_out));
        } else {
            auto promise = std::exchange(mPromises[WRITE_INDEX], std::nullopt);

            if (!promise)
                return;

            promise->reject(make_error_code(std::errc::timed_out));
        }
    }
}

std::error_code asyncio::ev::Buffer::getError() {
    return {EVUTIL_SOCKET_ERROR(), std::system_category()};
}

size_t asyncio::ev::Buffer::capacity() {
    return mCapacity;
}

tl::expected<asyncio::ev::Buffer, std::error_code>
asyncio::ev::makeBuffer(FileDescriptor fd, size_t capacity, bool own) {
    bufferevent *bev = bufferevent_socket_new(getEventLoop()->base(), fd, own ? BEV_OPT_CLOSE_ON_FREE : 0);

    if (!bev)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return Buffer{bev, capacity};
}

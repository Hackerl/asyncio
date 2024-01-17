#include <asyncio/ev/buffer.h>
#include <asyncio/event_loop.h>
#include <asyncio/error.h>
#include <optional>
#include <cassert>

asyncio::ev::Buffer::Buffer(bufferevent *bev, const std::size_t capacity) : Buffer({bev, bufferevent_free}, capacity) {
}

asyncio::ev::Buffer::Buffer(std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev, const std::size_t capacity)
    : mClosed(false), mCapacity(capacity), mBev(std::move(bev)) {
    bufferevent_setcb(
        mBev.get(),
        [](bufferevent *, void *arg) {
            const auto promise = std::move(static_cast<Buffer *>(arg)->mPromises[READ_INDEX]);

            if (!promise)
                return;

            promise->resolve();
        },
        [](bufferevent *, void *arg) {
            const auto promise = std::move(static_cast<Buffer *>(arg)->mPromises[WRITE_INDEX]);

            if (!promise)
                return;

            promise->resolve();
        },
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

    bufferevent_data_cb rcb, wcb;
    bufferevent_event_cb ecb;

    bufferevent_getcb(mBev.get(), &rcb, &wcb, &ecb, nullptr);
    bufferevent_setcb(mBev.get(), rcb, wcb, ecb, this);
}

asyncio::ev::Buffer::~Buffer() {
    assert(!mPromises[READ_INDEX]);
    assert(!mPromises[WRITE_INDEX]);
}

void asyncio::ev::Buffer::resize(const std::size_t capacity) {
    assert(!mPromises[READ_INDEX]);
    assert(!mPromises[WRITE_INDEX]);

    mCapacity = capacity;
    bufferevent_setwatermark(mBev.get(), EV_READ, 0, mCapacity);
    bufferevent_setwatermark(mBev.get(), EV_WRITE, mCapacity, 0);
}

void asyncio::ev::Buffer::onEvent(const short what) {
    if (what & BEV_EVENT_EOF) {
        onClose(IO_EOF);
    }
    else if (what & BEV_EVENT_ERROR) {
        onClose(getError());
    }
    else if (what & BEV_EVENT_TIMEOUT) {
        if (what & BEV_EVENT_READING) {
            const auto promise = std::move(mPromises[READ_INDEX]);

            if (!promise)
                return;

            promise->reject(make_error_code(std::errc::timed_out));
        }
        else {
            const auto promise = std::move(mPromises[WRITE_INDEX]);

            if (!promise)
                return;

            promise->reject(make_error_code(std::errc::timed_out));
        }
    }
}


void asyncio::ev::Buffer::onClose(const std::error_code &ec) {
    mClosed = true;
    mLastError = ec;

    for (const auto &promise: std::exchange(mPromises, {})) {
        if (!promise)
            continue;

        promise->reject(ec);
    }
}

std::error_code asyncio::ev::Buffer::getError() const {
    return {EVUTIL_SOCKET_ERROR(), std::system_category()};
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::close() {
    if (!mBev)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    assert(!mPromises[READ_INDEX]);
    assert(!mPromises[WRITE_INDEX]);

    if (!mClosed)
        co_await flush();

    mBev.reset();
    co_return tl::expected<void, std::error_code>{};
}

zero::async::coroutine::Task<std::size_t, std::error_code> asyncio::ev::Buffer::read(std::span<std::byte> data) {
    if (!mBev)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mPromises[READ_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    evbuffer *input = bufferevent_get_input(mBev.get());
    std::size_t length = evbuffer_get_length(input);

    if (length > 0) {
        std::size_t n = (std::min)(length, data.size());
        evbuffer_remove(input, data.data(), n);
        co_return n;
    }

    if (mClosed)
        co_return tl::unexpected(mLastError);

    if (const auto result = co_await zero::async::coroutine::Cancellable{
        zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
            mPromises[READ_INDEX] = promise;
            bufferevent_enable(mBev.get(), EV_READ);
        }),
        [this]() -> tl::expected<void, std::error_code> {
            std::exchange(mPromises[READ_INDEX], nullptr)->reject(
                make_error_code(std::errc::operation_canceled)
            );
            return {};
        }
    }; !result)
        co_return tl::unexpected(result.error());

    length = evbuffer_get_length(input);
    std::size_t n = (std::min)(length, data.size());

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
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mPromises[READ_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

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
            zero::async::promise::chain<void, std::error_code>([this](const auto &promise) {
                mPromises[READ_INDEX] = promise;
                bufferevent_enable(mBev.get(), EV_READ);
                bufferevent_setwatermark(mBev.get(), EV_READ, 0, 0);
            })->finally([this] {
                bufferevent_setwatermark(mBev.get(), EV_READ, 0, mCapacity);
            }),
            [this]() -> tl::expected<void, std::error_code> {
                std::exchange(mPromises[READ_INDEX], nullptr)->reject(
                    make_error_code(std::errc::operation_canceled)
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

zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> asyncio::ev::Buffer::readUntil(std::byte byte) {
    co_return tl::unexpected(make_error_code(std::errc::function_not_supported));
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::peek(std::span<std::byte> data) {
    if (!mBev)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

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

    if (const auto result = co_await zero::async::coroutine::Cancellable{
        zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
            mPromises[READ_INDEX] = promise;
            bufferevent_setwatermark(mBev.get(), EV_READ, data.size(), mCapacity);
            bufferevent_enable(mBev.get(), EV_READ);
        })->finally([this] {
            bufferevent_setwatermark(mBev.get(), EV_READ, 0, mCapacity);
        }),
        [this]() -> tl::expected<void, std::error_code> {
            std::exchange(mPromises[READ_INDEX], nullptr)->reject(
                make_error_code(std::errc::operation_canceled)
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
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mClosed)
        co_return tl::unexpected(make_error_code(std::errc::broken_pipe));

    if (mPromises[WRITE_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    evbuffer *output = bufferevent_get_output(mBev.get());
    tl::expected<std::size_t, std::error_code> result;

    while (*result < data.size()) {
        const std::size_t length = evbuffer_get_length(output);

        if (length >= mCapacity) {
            if (const auto res = co_await zero::async::coroutine::Cancellable{
                zero::async::promise::chain<void, std::error_code>([=, this](const auto &promise) {
                    mPromises[WRITE_INDEX] = promise;
                    bufferevent_enable(mBev.get(), EV_WRITE);
                }),
                [this]() -> tl::expected<void, std::error_code> {
                    if (!mPromises[WRITE_INDEX])
                        return tl::unexpected(make_error_code(std::errc::operation_not_supported));

                    std::exchange(mPromises[WRITE_INDEX], nullptr)->reject(
                        make_error_code(std::errc::operation_canceled)
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

size_t asyncio::ev::Buffer::pending() {
    if (!mBev)
        return -1;

    return evbuffer_get_length(bufferevent_get_output(mBev.get()));
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::flush() {
    if (!mBev)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mClosed)
        co_return tl::unexpected(make_error_code(std::errc::broken_pipe));

    if (mPromises[WRITE_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    if (evbuffer_get_length(bufferevent_get_output(mBev.get())) == 0)
        co_return tl::expected<void, std::error_code>{};

    co_return co_await zero::async::coroutine::Cancellable{
        zero::async::promise::chain<void, std::error_code>([this](const auto &promise) {
            mPromises[WRITE_INDEX] = promise;
            bufferevent_setwatermark(mBev.get(), EV_WRITE, 0, 0);
        })->finally([this] {
            bufferevent_setwatermark(mBev.get(), EV_WRITE, mCapacity, 0);
        }),
        [this]() -> tl::expected<void, std::error_code> {
            if (!mPromises[WRITE_INDEX])
                return tl::unexpected(make_error_code(std::errc::operation_not_supported));

            std::exchange(mPromises[WRITE_INDEX], nullptr)->reject(
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

void asyncio::ev::Buffer::setTimeout(const std::chrono::milliseconds timeout) {
    setTimeout(timeout, timeout);
}

void asyncio::ev::Buffer::setTimeout(
    const std::chrono::milliseconds readTimeout,
    const std::chrono::milliseconds writeTimeout
) {
    if (!mBev)
        return;

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
}

size_t asyncio::ev::Buffer::capacity() {
    return mCapacity;
}

tl::expected<asyncio::ev::Buffer, std::error_code>
asyncio::ev::makeBuffer(const FileDescriptor fd, const std::size_t capacity, const bool own) {
    bufferevent *bev = bufferevent_socket_new(getEventLoop()->base(), fd, own ? BEV_OPT_CLOSE_ON_FREE : 0);

    if (!bev)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return Buffer{bev, capacity};
}

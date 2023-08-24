#include <asyncio/ev/buffer.h>
#include <asyncio/event_loop.h>
#include <asyncio/error.h>
#include <optional>
#include <cassert>

constexpr auto READ_INDEX = 0;
constexpr auto DRAIN_INDEX = 1;
constexpr auto WAIT_CLOSED_INDEX = 2;

asyncio::ev::Buffer::Buffer(bufferevent *bev) : Buffer({bev, bufferevent_free}) {

}

asyncio::ev::Buffer::Buffer(std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev)
        : mBev(std::move(bev)), mClosed(false) {
    bufferevent_setcb(
            mBev.get(),
            [](bufferevent *bev, void *arg) {
                static_cast<Buffer *>(arg)->onBufferRead();
            },
            [](bufferevent *bev, void *arg) {
                static_cast<Buffer *>(arg)->onBufferWrite();
            },
            [](bufferevent *bev, short what, void *arg) {
                static_cast<Buffer *>(arg)->onBufferEvent(what);
            },
            this
    );

    bufferevent_enable(mBev.get(), EV_READ | EV_WRITE);
    bufferevent_setwatermark(mBev.get(), EV_READ | EV_WRITE, 0, 0);
}

asyncio::ev::Buffer::Buffer(asyncio::ev::Buffer &&rhs) noexcept
        : mBev(std::move(rhs.mBev)), mClosed(rhs.mClosed) {
    assert(!mPromises[READ_INDEX]);
    assert(!mPromises[DRAIN_INDEX]);
    assert(!mPromises[WAIT_CLOSED_INDEX]);

    bufferevent_data_cb rcb, wcb;
    bufferevent_event_cb ecb;

    bufferevent_getcb(mBev.get(), &rcb, &wcb, &ecb, nullptr);
    bufferevent_setcb(mBev.get(), rcb, wcb, ecb, this);
}

asyncio::ev::Buffer::~Buffer() {
    assert(!mPromises[READ_INDEX]);
    assert(!mPromises[DRAIN_INDEX]);
    assert(!mPromises[WAIT_CLOSED_INDEX]);
}

size_t asyncio::ev::Buffer::available() {
    if (!mBev)
        return -1;

    return evbuffer_get_length(bufferevent_get_input(mBev.get()));
}

zero::async::coroutine::Task<std::string, std::error_code> asyncio::ev::Buffer::readLine() {
    return readLine(EOL::CRLF);
}

zero::async::coroutine::Task<std::string, std::error_code> asyncio::ev::Buffer::readLine(EOL eol) {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromises[WAIT_CLOSED_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    if (mPromises[READ_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    evbuffer *input = bufferevent_get_input(mBev.get());
    tl::expected<std::string, std::error_code> result;

    while (true) {
        char *ptr = evbuffer_readln(input, nullptr, (evbuffer_eol_style) eol);

        if (ptr) {
            result = std::unique_ptr<char, decltype(free) *>(ptr, free).get();
            break;
        }

        if (mClosed) {
            result = tl::unexpected<std::error_code>(Error::IO_EOF);
            break;
        }

        auto res = co_await zero::async::coroutine::Cancellable{
                zero::async::promise::chain<void, std::error_code>([this](const auto &promise) {
                    mPromises[READ_INDEX] = promise;

                    bufferevent_setwatermark(mBev.get(), EV_READ, 0, 0);
                    bufferevent_enable(mBev.get(), EV_READ);
                }).finally([this]() {
                    bufferevent_disable(mBev.get(), EV_READ);
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

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::peek(std::span<std::byte> data) {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromises[WAIT_CLOSED_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    if (mPromises[READ_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    evbuffer *input = bufferevent_get_input(mBev.get());
    size_t length = evbuffer_get_length(input);

    if (length >= data.size()) {
        evbuffer_copyout(input, data.data(), data.size());
        co_return tl::expected<void, std::error_code>{};
    }

    if (mClosed)
        co_return tl::unexpected(Error::IO_EOF);

    auto result = co_await zero::async::coroutine::Cancellable{
            zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
                mPromises[READ_INDEX] = promise;

                bufferevent_setwatermark(mBev.get(), EV_READ, data.size(), 0);
                bufferevent_enable(mBev.get(), EV_READ);
            }).finally([this]() {
                bufferevent_disable(mBev.get(), EV_READ);
                bufferevent_setwatermark(mBev.get(), EV_READ, 0, 0);
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

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::readExactly(std::span<std::byte> data) {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromises[WAIT_CLOSED_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    if (mPromises[READ_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    evbuffer *input = bufferevent_get_input(mBev.get());
    size_t length = evbuffer_get_length(input);

    if (length >= data.size()) {
        evbuffer_remove(input, data.data(), data.size());
        co_return tl::expected<void, std::error_code>{};
    }

    if (mClosed)
        co_return tl::unexpected(Error::IO_EOF);

    auto result = co_await zero::async::coroutine::Cancellable{
            zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
                mPromises[READ_INDEX] = promise;

                bufferevent_setwatermark(mBev.get(), EV_READ, data.size(), 0);
                bufferevent_enable(mBev.get(), EV_READ);
            }).finally([this]() {
                bufferevent_disable(mBev.get(), EV_READ);
                bufferevent_setwatermark(mBev.get(), EV_READ, 0, 0);
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

    evbuffer_remove(input, data.data(), data.size());
    co_return tl::expected<void, std::error_code>{};
}

tl::expected<void, std::error_code> asyncio::ev::Buffer::writeLine(std::string_view line) {
    return writeLine(line, EOL::CRLF);
}

tl::expected<void, std::error_code> asyncio::ev::Buffer::writeLine(std::string_view line, EOL eol) {
    tl::expected<void, std::error_code> result = submit({(const std::byte *) line.data(), line.length()});

    if (!result)
        return result;

    switch (eol) {
        case CRLF:
        case CRLF_STRICT: {
            auto bytes = {std::byte{'\r'}, std::byte{'\n'}};
            result = submit(bytes);
            break;
        }

        case LF: {
            auto bytes = {std::byte{'\n'}};
            result = submit(bytes);
            break;
        }

        case NUL: {
            auto bytes = {std::byte{0}};
            result = submit(bytes);
            break;
        }

        default:
            result = tl::unexpected(make_error_code(std::errc::invalid_argument));
            break;
    }

    return result;
}

tl::expected<void, std::error_code> asyncio::ev::Buffer::submit(std::span<const std::byte> data) {
    if (!mBev)
        return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mClosed)
        return tl::unexpected(Error::IO_EOF);

    bufferevent_write(mBev.get(), data.data(), data.size());
    return {};
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::drain() {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromises[DRAIN_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    if (mClosed)
        co_return tl::unexpected(Error::IO_EOF);

    evbuffer *output = bufferevent_get_output(mBev.get());

    if (evbuffer_get_length(output) == 0)
        co_return tl::expected<void, std::error_code>{};

    co_return co_await zero::async::coroutine::Cancellable{
            zero::async::promise::chain<void, std::error_code>([this](const auto &promise) {
                mPromises[DRAIN_INDEX] = promise;
            }),
            [this]() -> tl::expected<void, std::error_code> {
                std::exchange(mPromises[DRAIN_INDEX], std::nullopt)->reject(
                        make_error_code(std::errc::operation_canceled)
                );
                return {};
            }
    };
}

size_t asyncio::ev::Buffer::pending() {
    if (!mBev)
        return -1;

    return evbuffer_get_length(bufferevent_get_output(mBev.get()));
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::waitClosed() {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromises[READ_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    if (mPromises[WAIT_CLOSED_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    if (mClosed)
        co_return tl::unexpected(Error::IO_EOF);

    co_return co_await zero::async::coroutine::Cancellable{
            zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
                mPromises[WAIT_CLOSED_INDEX] = promise;

                bufferevent_enable(mBev.get(), EV_READ);
                bufferevent_set_timeouts(mBev.get(), nullptr, nullptr);
            }).finally([this]() {
                bufferevent_disable(mBev.get(), EV_READ);
            }),
            [this]() -> tl::expected<void, std::error_code> {
                std::exchange(mPromises[WAIT_CLOSED_INDEX], std::nullopt)->reject(
                        make_error_code(std::errc::operation_canceled)
                );
                return {};
            }
    };
}

evutil_socket_t asyncio::ev::Buffer::fd() {
    if (!mBev)
        return EVUTIL_INVALID_SOCKET;

    return bufferevent_getfd(mBev.get());
}

zero::async::coroutine::Task<size_t, std::error_code> asyncio::ev::Buffer::read(std::span<std::byte> data) {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromises[WAIT_CLOSED_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

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
        co_return tl::unexpected(Error::IO_EOF);

    auto result = co_await zero::async::coroutine::Cancellable{
            zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
                mPromises[READ_INDEX] = promise;

                bufferevent_setwatermark(mBev.get(), EV_READ, 0, 0);
                bufferevent_enable(mBev.get(), EV_READ);
            }).finally([this]() {
                bufferevent_disable(mBev.get(), EV_READ);
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

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::write(std::span<const std::byte> data) {
    tl::expected<void, std::error_code> result = submit(data);

    if (!result)
        co_return tl::unexpected(result.error());

    co_return co_await drain();
}

tl::expected<void, std::error_code> asyncio::ev::Buffer::close() {
    if (!mBev)
        return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mClosed)
        return tl::unexpected(Error::IO_EOF);

    onClose(Error::IO_EOF);
    mBev.reset();

    return {};
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

    auto [read, drain, waitClosed] = std::exchange(mPromises, {});

    if (read)
        read->reject(ec);

    if (drain)
        drain->reject(ec);

    if (!waitClosed)
        return;

    if (ec != Error::IO_EOF) {
        waitClosed->reject(ec);
        return;
    }

    waitClosed->resolve();
}

void asyncio::ev::Buffer::onBufferRead() {
    auto promise = std::exchange(mPromises[READ_INDEX], std::nullopt);

    if (!promise) {
        if (mPromises[WAIT_CLOSED_INDEX])
            return;

        if (available() < 1024 * 1024)
            return;

        bufferevent_disable(mBev.get(), EV_READ);
        return;
    }

    promise->resolve();
}

void asyncio::ev::Buffer::onBufferWrite() {
    auto promise = std::exchange(mPromises[DRAIN_INDEX], std::nullopt);

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
            auto promise = std::exchange(mPromises[DRAIN_INDEX], std::nullopt);

            if (!promise)
                return;

            promise->reject(make_error_code(std::errc::timed_out));
        }
    }
}

std::error_code asyncio::ev::Buffer::getError() {
    return {EVUTIL_SOCKET_ERROR(), std::system_category()};
}

tl::expected<asyncio::ev::Buffer, std::error_code> asyncio::ev::makeBuffer(evutil_socket_t fd, bool own) {
    bufferevent *bev = bufferevent_socket_new(getEventLoop()->base(), fd, own ? BEV_OPT_CLOSE_ON_FREE : 0);

    if (!bev)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return Buffer{bev};
}

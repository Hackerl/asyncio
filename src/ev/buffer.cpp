#include <asyncio/ev/buffer.h>
#include <asyncio/event_loop.h>
#include <asyncio/error.h>
#include <optional>

constexpr auto READ_INDEX = 0;
constexpr auto DRAIN_INDEX = 1;
constexpr auto WAIT_CLOSED_INDEX = 2;

asyncio::ev::Buffer::Buffer(bufferevent *bev) : mBev(bev), mClosed(false) {
    bufferevent_setcb(
            mBev,
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

    bufferevent_enable(mBev, EV_READ | EV_WRITE);
    bufferevent_setwatermark(mBev, EV_READ | EV_WRITE, 0, 0);
}

asyncio::ev::Buffer::~Buffer() {
    if (mBev) {
        bufferevent_free(mBev);
        mBev = nullptr;
    }
}

size_t asyncio::ev::Buffer::available() {
    if (!mBev)
        return -1;

    return evbuffer_get_length(bufferevent_get_input(mBev));
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

    evbuffer *input = bufferevent_get_input(mBev);
    tl::expected<std::string, std::error_code> result;

    while (true) {
        char *ptr = evbuffer_readln(input, nullptr, (evbuffer_eol_style) eol);

        if (ptr) {
            result = std::unique_ptr<char>(ptr).get();
            break;
        }

        if (mClosed) {
            result = tl::unexpected<std::error_code>(Error::IO_EOF);
            break;
        }

        auto res = co_await zero::async::promise::chain<void, std::error_code>([this](const auto &promise) {
            mPromises[READ_INDEX] = std::make_unique<zero::async::promise::Promise<void, std::error_code>>(promise);

            bufferevent_setwatermark(mBev, EV_READ, 0, 0);
            bufferevent_enable(mBev, EV_READ);
        }).finally([this]() {
            bufferevent_disable(mBev, EV_READ);
        });

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

    evbuffer *input = bufferevent_get_input(mBev);
    size_t length = evbuffer_get_length(input);

    if (length >= data.size()) {
        evbuffer_copyout(input, data.data(), data.size());
        co_return tl::expected<void, std::error_code>{};
    }

    if (mClosed)
        co_return tl::unexpected(Error::IO_EOF);

    auto result = co_await zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
        mPromises[READ_INDEX] = std::make_unique<zero::async::promise::Promise<void, std::error_code>>(promise);

        bufferevent_setwatermark(mBev, EV_READ, data.size(), 0);
        bufferevent_enable(mBev, EV_READ);
    }).finally([this]() {
        bufferevent_disable(mBev, EV_READ);
        bufferevent_setwatermark(mBev, EV_READ, 0, 0);
    });

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

    evbuffer *input = bufferevent_get_input(mBev);
    size_t length = evbuffer_get_length(input);

    if (length >= data.size()) {
        evbuffer_remove(input, data.data(), data.size());
        co_return tl::expected<void, std::error_code>{};
    }

    if (mClosed)
        co_return tl::unexpected(Error::IO_EOF);

    auto result = co_await zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
        mPromises[READ_INDEX] = std::make_unique<zero::async::promise::Promise<void, std::error_code>>(promise);

        bufferevent_setwatermark(mBev, EV_READ, data.size(), 0);
        bufferevent_enable(mBev, EV_READ);
    }).finally([this]() {
        bufferevent_disable(mBev, EV_READ);
        bufferevent_setwatermark(mBev, EV_READ, 0, 0);
    });

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
    if (mClosed)
        return tl::unexpected(Error::IO_EOF);

    bufferevent_write(mBev, data.data(), data.size());
    return {};
}

zero::async::coroutine::Task<void, std::error_code> asyncio::ev::Buffer::drain() {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromises[DRAIN_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    if (mClosed)
        co_return tl::unexpected(Error::IO_EOF);

    evbuffer *output = bufferevent_get_output(mBev);

    if (evbuffer_get_length(output) == 0)
        co_return tl::expected<void, std::error_code>{};

    co_return co_await zero::async::promise::chain<void, std::error_code>([this](const auto &promise) {
        mPromises[DRAIN_INDEX] = std::make_unique<zero::async::promise::Promise<void, std::error_code>>(promise);
    });
}

size_t asyncio::ev::Buffer::pending() {
    if (!mBev)
        return -1;

    return evbuffer_get_length(bufferevent_get_output(mBev));
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

    co_return co_await zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
        mPromises[WAIT_CLOSED_INDEX] = std::make_unique<zero::async::promise::Promise<void, std::error_code>>(promise);

        bufferevent_enable(mBev, EV_READ);
        bufferevent_set_timeouts(mBev, nullptr, nullptr);
    }).finally([this]() {
        bufferevent_disable(mBev, EV_READ);
    });
}

evutil_socket_t asyncio::ev::Buffer::fd() {
    if (!mBev)
        return -1;

    return bufferevent_getfd(mBev);
}

zero::async::coroutine::Task<size_t, std::error_code> asyncio::ev::Buffer::read(std::span<std::byte> data) {
    if (!mBev)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromises[WAIT_CLOSED_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    if (mPromises[READ_INDEX])
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    evbuffer *input = bufferevent_get_input(mBev);
    size_t length = evbuffer_get_length(input);

    if (length > 0) {
        size_t n = (std::min)(length, data.size());
        evbuffer_remove(input, data.data(), n);
        co_return n;
    }

    if (mClosed)
        co_return tl::unexpected(Error::IO_EOF);

    auto result = co_await zero::async::promise::chain<void, std::error_code>([&](const auto &promise) {
        mPromises[READ_INDEX] = std::make_unique<zero::async::promise::Promise<void, std::error_code>>(promise);

        bufferevent_setwatermark(mBev, EV_READ, 0, 0);
        bufferevent_enable(mBev, EV_READ);
    }).finally([this]() {
        bufferevent_disable(mBev, EV_READ);
    });

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
    if (mClosed)
        return tl::unexpected(Error::IO_EOF);

    onClose(Error::IO_EOF);

    bufferevent_free(mBev);
    mBev = nullptr;

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
        rtv = timeval{
                (time_t) (readTimeout.count() / 1000),
                (suseconds_t) ((readTimeout.count() % 1000) * 1000)
        };

    if (writeTimeout != std::chrono::milliseconds::zero())
        wtv = timeval{
                (time_t) (writeTimeout.count() / 1000),
                (suseconds_t) ((writeTimeout.count() % 1000) * 1000)
        };

    bufferevent_set_timeouts(
            mBev,
            rtv ? &*rtv : nullptr,
            wtv ? &*wtv : nullptr
    );
}

void asyncio::ev::Buffer::onClose(const std::error_code &ec) {
    mClosed = true;

    auto [read, drain, waitClosed] = std::move(mPromises);

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
    auto promise = std::move(mPromises[READ_INDEX]);

    if (!promise) {
        if (mPromises[WAIT_CLOSED_INDEX])
            return;

        if (available() < 1024 * 1024)
            return;

        bufferevent_disable(mBev, EV_READ);
        return;
    }

    promise->resolve();
}

void asyncio::ev::Buffer::onBufferWrite() {
    auto promise = std::move(mPromises[DRAIN_INDEX]);

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
            auto promise = std::move(mPromises[READ_INDEX]);

            if (!promise)
                return;

            promise->reject(make_error_code(std::errc::timed_out));
        } else {
            auto promise = std::move(mPromises[DRAIN_INDEX]);

            if (!promise)
                return;

            promise->reject(make_error_code(std::errc::timed_out));
        }
    }
}

std::error_code asyncio::ev::Buffer::getError() {
    return {EVUTIL_SOCKET_ERROR(), std::system_category()};
}

std::shared_ptr<asyncio::ev::IBuffer> asyncio::ev::newBuffer(int fd, bool own) {
    bufferevent *bev = bufferevent_socket_new(getEventLoop()->base(), fd, own ? BEV_OPT_CLOSE_ON_FREE : 0);

    if (!bev)
        return nullptr;

    return std::make_shared<Buffer>(bev);
}

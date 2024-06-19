#include <asyncio/stream.h>
#include <asyncio/promise.h>

asyncio::Stream::Stream(uv::Handle<uv_stream_t> stream) : mStream(std::move(stream)) {
}

asyncio::uv::Handle<uv_stream_s> &asyncio::Stream::handle() {
    return mStream;
}

const asyncio::uv::Handle<uv_stream_s> &asyncio::Stream::handle() const {
    return mStream;
}

zero::async::coroutine::Task<std::size_t, std::error_code> asyncio::Stream::read(const std::span<std::byte> data) {
    struct Context {
        std::span<std::byte> data;
        Promise<std::size_t, std::error_code> promise;
    };

    Context context = {data};
    mStream->data = &context;

    CO_EXPECT(uv::expected([&] {
        return uv_read_start(
            mStream.raw(),
            [](const auto handle, const size_t, uv_buf_t *buf) {
                const auto span = static_cast<Context *>(handle->data)->data;
                buf->base = reinterpret_cast<char *>(span.data());
                buf->len = static_cast<decltype(uv_buf_t::len)>(span.size());
            },
            [](uv_stream_t *handle, const ssize_t n, const uv_buf_t *) {
                uv_read_stop(handle);
                const auto ctx = static_cast<Context *>(handle->data);

                if (n < 0) {
                    if (n == UV_EOF) {
                        ctx->promise.resolve(0);
                        return;
                    }

                    ctx->promise.reject(static_cast<uv::Error>(n));
                    return;
                }

                ctx->promise.resolve(static_cast<std::size_t>(n));
            }
        );
    }));

    co_return co_await zero::async::coroutine::Cancellable{
        context.promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            if (context.promise.isFulfilled())
                return std::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

            uv_read_stop(mStream.raw());
            context.promise.reject(zero::async::coroutine::Error::CANCELLED);
            return {};
        }
    };
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::Stream::write(const std::span<const std::byte> data) {
    Promise<void, std::error_code> promise;
    uv_write_t request = {.data = &promise};

    CO_EXPECT(uv::expected([&] {
        uv_buf_t buffer;

        buffer.base = reinterpret_cast<char *>(const_cast<std::byte *>(data.data()));
        buffer.len = static_cast<decltype(uv_buf_t::len)>(data.size());

        return uv_write(
            &request,
            mStream.raw(),
            &buffer,
            1,
            [](const auto req, const int status) {
                const auto p = static_cast<Promise<void, std::error_code> *>(req->data);

                if (status < 0) {
                    p->reject(static_cast<uv::Error>(status));
                    return;
                }

                p->resolve();
            }
        );
    }));

    if (const auto result = co_await promise.getFuture(); !result) {
        const std::size_t size = data.size();
        const std::size_t queueSize = uv_stream_get_write_queue_size(mStream.raw());
        assert(queueSize <= size);

        if (queueSize < size)
            co_return size - queueSize;

        co_return std::unexpected(result.error());
    }

    co_return data.size();
}

asyncio::Listener::Listener(std::unique_ptr<Context> context) : mContext(std::move(context)) {
}

std::expected<asyncio::Listener, std::error_code> asyncio::Listener::make(uv::Handle<uv_stream_t> stream) {
    EXPECT(uv::expected([&] {
        return uv_listen(
            stream.raw(),
            256,
            [](const auto handle, const int status) {
                auto &[stream, event, ec] = *static_cast<Context *>(handle->data);

                if (status < 0)
                    ec = static_cast<uv::Error>(status);

                event.set();
            }
        );
    }));

    auto context = std::make_unique<Context>(std::move(stream));
    context->stream->data = context.get();

    return Listener{std::move(context)};
}

asyncio::uv::Handle<uv_stream_s> &asyncio::Listener::handle() {
    return mContext->stream;
}

const asyncio::uv::Handle<uv_stream_s> &asyncio::Listener::handle() const {
    return mContext->stream;
}

// ReSharper disable once CppMemberFunctionMayBeConst
zero::async::coroutine::Task<void, std::error_code> asyncio::Listener::accept(uv_stream_t *stream) {
    while (true) {
        const auto result = uv::expected([&] {
            return uv_accept(mContext->stream.raw(), stream);
        });

        if (result)
            co_return {};

        if (result.error() != std::errc::resource_unavailable_try_again)
            co_return std::unexpected(result.error());

        mContext->event.reset();
        CO_EXPECT(co_await mContext->event.wait());

        if (mContext->ec)
            co_return std::unexpected(*mContext->ec);
    }
}

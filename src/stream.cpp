#include <asyncio/stream.h>
#include <zero/defer.h>

#ifndef _WIN32
#include <unistd.h>
#endif

asyncio::Stream::Stream(uv::Handle<uv_stream_t> stream) : mStream{std::move(stream)} {
}

std::expected<std::array<asyncio::Stream, 2>, std::error_code> asyncio::Stream::pair() {
    std::array<uv_os_sock_t, 2> sockets{};

    EXPECT(uv::expected([&] {
        return uv_socketpair(SOCK_STREAM, 0, sockets.data(), UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE);
    }));
#ifdef _WIN32
    DEFER(
        for (const auto &fd: sockets) {
            if (fd == -1)
                continue;

            closesocket(fd);
        }
    );

    std::unique_ptr<uv_tcp_t, decltype(&std::free)> first{
        static_cast<uv_tcp_t *>(std::malloc(sizeof(uv_tcp_t))),
        std::free
    };

    if (!first)
        return std::unexpected{std::error_code{errno, std::generic_category()}};

    EXPECT(uv::expected([&] {
        return uv_tcp_init(getEventLoop()->raw(), first.get());
    }));

    uv::Handle firstHandle{
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(first.release()),
            std::free
        }
    };

    EXPECT(uv::expected([&] {
        return uv_tcp_open(reinterpret_cast<uv_tcp_t *>(firstHandle.raw()), sockets[0]);
    }));
    sockets[0] = -1;

    std::unique_ptr<uv_tcp_t, decltype(&std::free)> second{
        static_cast<uv_tcp_t *>(std::malloc(sizeof(uv_tcp_t))),
        std::free
    };

    if (!second)
        return std::unexpected{std::error_code{errno, std::generic_category()}};

    EXPECT(uv::expected([&] {
        return uv_tcp_init(getEventLoop()->raw(), second.get());
    }));

    uv::Handle secondHandle{
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(second.release()),
            std::free
        }
    };

    EXPECT(uv::expected([&] {
        return uv_tcp_open(reinterpret_cast<uv_tcp_t *>(secondHandle.raw()), sockets[1]);
    }));
    sockets[1] = -1;

    return std::array{Stream{std::move(firstHandle)}, Stream{std::move(secondHandle)}};
#else
    DEFER(
        for (const auto &fd: sockets) {
            if (fd == -1)
                continue;

            ::close(fd);
        }
    );

    std::unique_ptr<uv_pipe_t, decltype(&std::free)> first{
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    };

    if (!first)
        return std::unexpected{std::error_code{errno, std::generic_category()}};

    EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), first.get(), 0);
    }));

    uv::Handle firstHandle{
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(first.release()),
            std::free
        }
    };

    EXPECT(uv::expected([&] {
        return uv_pipe_open(reinterpret_cast<uv_pipe_t *>(firstHandle.raw()), sockets[0]);
    }));
    sockets[0] = -1;

    std::unique_ptr<uv_pipe_t, decltype(&std::free)> second{
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    };

    if (!second)
        return std::unexpected{std::error_code{errno, std::generic_category()}};

    EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), second.get(), 0);
    }));

    uv::Handle secondHandle{
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(second.release()),
            std::free
        }
    };

    EXPECT(uv::expected([&] {
        return uv_pipe_open(reinterpret_cast<uv_pipe_t *>(secondHandle.raw()), sockets[1]);
    }));
    sockets[1] = -1;

    return std::array{Stream{std::move(firstHandle)}, Stream{std::move(secondHandle)}};
#endif
}

asyncio::task::Task<void, std::error_code> asyncio::Stream::shutdown() {
    Promise<void, std::error_code> promise;
    uv_shutdown_t request{.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_shutdown(
            &request,
            mStream.raw(),
            [](auto *req, const int status) {
                const auto p = static_cast<Promise<void, std::error_code> *>(req->data);

                if (status < 0) {
                    p->reject(static_cast<uv::Error>(status));
                    return;
                }

                p->resolve();
            }
        );
    }));

    co_return co_await promise.getFuture();
}

asyncio::task::Task<std::size_t, std::error_code> asyncio::Stream::read(const std::span<std::byte> data) {
    struct Context {
        std::span<std::byte> data;
        Promise<std::size_t, std::error_code> promise;
    };

    Context context{data};
    mStream->data = &context;

    CO_EXPECT(uv::expected([&] {
        return uv_read_start(
            mStream.raw(),
            [](auto *handle, const size_t, uv_buf_t *buf) {
                const auto span = static_cast<const Context *>(handle->data)->data;
                buf->base = reinterpret_cast<char *>(span.data());
                buf->len = static_cast<decltype(uv_buf_t::len)>(span.size());
            },
            [](auto *handle, const ssize_t n, const uv_buf_t *) {
                uv_read_stop(handle);
                auto &promise = static_cast<Context *>(handle->data)->promise;

                if (n < 0) {
                    if (n == UV_EOF) {
                        promise.resolve(0);
                        return;
                    }

                    promise.reject(static_cast<uv::Error>(n));
                    return;
                }

                promise.resolve(static_cast<std::size_t>(n));
            }
        );
    }));

    co_return co_await task::Cancellable{
        context.promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            if (context.promise.isFulfilled())
                return std::unexpected{task::Error::WILL_BE_DONE};

            uv_read_stop(mStream.raw());
            context.promise.reject(task::Error::CANCELLED);
            return {};
        }
    };
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::Stream::write(const std::span<const std::byte> data) {
    Promise<void, std::error_code> promise;
    uv_write_t request{.data = &promise};

    CO_EXPECT(uv::expected([&] {
        uv_buf_t buffer;

        buffer.base = reinterpret_cast<char *>(const_cast<std::byte *>(data.data()));
        buffer.len = static_cast<decltype(uv_buf_t::len)>(data.size());

        return uv_write(
            &request,
            mStream.raw(),
            &buffer,
            1,
            [](auto *req, const int status) {
                const auto p = static_cast<Promise<void, std::error_code> *>(req->data);

                if (status < 0) {
                    p->reject(static_cast<uv::Error>(status));
                    return;
                }

                p->resolve();
            }
        );
    }));

    // if an error occurs but some data has been written, the amount written should be returned,
    // but I have no way of knowing how much data has been written.
    CO_EXPECT(co_await promise.getFuture());
    co_return data.size();
}

asyncio::task::Task<void, std::error_code> asyncio::Stream::close() {
    const auto handle = mStream.release();

    Promise<void> promise;
    handle->data = &promise;

    uv_close(
        reinterpret_cast<uv_handle_t *>(handle.get()),
        [](auto *h) {
            static_cast<Promise<void> *>(h->data)->resolve();
        }
    );

    co_await promise.getFuture();
    co_return {};
}

std::expected<std::size_t, std::error_code> asyncio::Stream::tryWrite(const std::span<const std::byte> data) {
    return uv::expected([&] {
        uv_buf_t buffer;

        buffer.base = reinterpret_cast<char *>(const_cast<std::byte *>(data.data()));
        buffer.len = static_cast<decltype(uv_buf_t::len)>(data.size());

        return uv_try_write(mStream.raw(), &buffer, 1);
    });
}

asyncio::Listener::Listener(std::unique_ptr<Core> core) : mCore{std::move(core)} {
}

std::expected<asyncio::Listener, std::error_code> asyncio::Listener::make(uv::Handle<uv_stream_t> stream) {
    EXPECT(uv::expected([&] {
        return uv_listen(
            stream.raw(),
            256,
            [](auto *handle, const int status) {
                auto &[stream, event, ec] = *static_cast<Core *>(handle->data);

                if (status < 0)
                    ec = static_cast<uv::Error>(status);

                event.set();
            }
        );
    }));

    auto context = std::make_unique<Core>(std::move(stream));
    context->stream->data = context.get();

    return Listener{std::move(context)};
}

// ReSharper disable once CppMemberFunctionMayBeConst
asyncio::task::Task<void, std::error_code> asyncio::Listener::accept(uv_stream_t *stream) {
    while (true) {
        const auto result = uv::expected([&] {
            return uv_accept(mCore->stream.raw(), stream);
        });

        if (result)
            co_return {};

        if (result.error() != std::errc::resource_unavailable_try_again)
            co_return std::unexpected{result.error()};

        mCore->event.reset();
        CO_EXPECT(co_await mCore->event.wait());

        if (mCore->ec)
            co_return std::unexpected{*mCore->ec};
    }
}

asyncio::task::Task<void, std::error_code> asyncio::Listener::close() {
    const auto handle = mCore->stream.release();
    mCore.reset();

    Promise<void> promise;
    handle->data = &promise;

    uv_close(
        reinterpret_cast<uv_handle_t *>(handle.get()),
        [](auto *h) {
            static_cast<Promise<void> *>(h->data)->resolve();
        }
    );

    co_await promise.getFuture();
    co_return {};
}

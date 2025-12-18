#include <asyncio/pipe.h>
#include <zero/defer.h>

asyncio::Pipe::Pipe(uv::Handle<uv_stream_t> stream) : Stream{std::move(stream)} {
}

std::expected<asyncio::Pipe, std::error_code> asyncio::Pipe::from(const uv_file file) {
    std::unique_ptr<uv_pipe_t, decltype(&std::free)> pipe{
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    };

    if (!pipe)
        throw zero::error::SystemError{errno, std::generic_category()};

    zero::error::guard(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    uv::Handle handle{
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(pipe.release()),
            std::free
        }
    };

    Z_EXPECT(uv::expected([&] {
        return uv_pipe_open(reinterpret_cast<uv_pipe_t *>(handle.raw()), file);
    }));

    return Pipe{std::move(handle)};
}

asyncio::FileDescriptor asyncio::Pipe::fd() const {
    const auto fd = mStream.fd();
    assert(fd);
    return *fd;
}

std::expected<std::string, std::error_code> asyncio::Pipe::localAddress() const {
    std::size_t size{1024};
    std::string address;

    address.resize(size);

    const auto result = uv::expected([&] {
        return uv_pipe_getsockname(reinterpret_cast<const uv_pipe_t *>(mStream.raw()), address.data(), &size);
    });

    if (result) {
        address.resize(size);
        return address;
    }

    if (result.error() != std::errc::no_buffer_space)
        return std::unexpected{result.error()};

    address.resize(size);

    Z_EXPECT(uv::expected([&] {
        return uv_pipe_getsockname(reinterpret_cast<const uv_pipe_t *>(mStream.raw()), address.data(), &size);
    }));

    assert(address.size() == size);
    return address;
}

std::expected<std::string, std::error_code> asyncio::Pipe::remoteAddress() const {
    std::size_t size{1024};
    std::string address;

    address.resize(size);

    const auto result = uv::expected([&] {
        return uv_pipe_getpeername(reinterpret_cast<const uv_pipe_t *>(mStream.raw()), address.data(), &size);
    });

    if (result) {
        address.resize(size);
        return address;
    }

    if (result.error() != std::errc::no_buffer_space)
        return std::unexpected{result.error()};

    address.resize(size);

    Z_EXPECT(uv::expected([&] {
        return uv_pipe_getpeername(reinterpret_cast<const uv_pipe_t *>(mStream.raw()), address.data(), &size);
    }));

    assert(address.size() == size);
    return address;
}

std::expected<std::array<asyncio::Pipe, 2>, std::error_code> asyncio::pipe() {
    std::array<uv_file, 2> fds{};

    Z_EXPECT(uv::expected([&] {
        return uv_pipe(fds.data(), UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE);
    }));

    Z_DEFER(
        for (const auto &fd: fds) {
            if (fd == -1)
                continue;

            uv_fs_t request{};

            zero::error::guard(uv::expected([&] {
                return uv_fs_close(nullptr, &request, fd, nullptr);
            }));

            uv_fs_req_cleanup(&request);
        }
    );

    auto reader = Pipe::from(fds[0]);
    Z_EXPECT(reader);
    fds[0] = -1;

    auto writer = Pipe::from(fds[1]);
    Z_EXPECT(writer);
    fds[1] = -1;

    return std::array{*std::move(reader), *std::move(writer)};
}

asyncio::PipeListener::PipeListener(Listener listener) : Listener(std::move(listener)) {
}

asyncio::FileDescriptor asyncio::PipeListener::fd() const {
    const auto fd = mCore->stream.fd();
    assert(fd);
    return *fd;
}

std::expected<std::string, std::error_code> asyncio::PipeListener::address() const {
    std::size_t size{1024};
    std::string address;

    address.resize(size);

    const auto result = uv::expected([&] {
        return uv_pipe_getsockname(reinterpret_cast<const uv_pipe_t *>(mCore->stream.raw()), address.data(), &size);
    });

    if (result) {
        address.resize(size);
        return address;
    }

    if (result.error() != std::errc::no_buffer_space)
        return std::unexpected{result.error()};

    address.resize(size);

    Z_EXPECT(uv::expected([&] {
        return uv_pipe_getsockname(reinterpret_cast<const uv_pipe_t *>(mCore->stream.raw()), address.data(), &size);
    }));

    assert(address.size() == size);
    return address;
}

// ReSharper disable once CppMemberFunctionMayBeConst
std::expected<void, std::error_code> asyncio::PipeListener::chmod(const int mode) {
    Z_EXPECT(uv::expected([&] {
        return uv_pipe_chmod(reinterpret_cast<uv_pipe_t *>(mCore->stream.raw()), mode);
    }));
    return {};
}

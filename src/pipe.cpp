#include <asyncio/pipe.h>
#include <zero/defer.h>

asyncio::Pipe::Pipe(uv::Handle<uv_stream_t> stream) : Stream(std::move(stream)) {
}

std::expected<asyncio::Pipe, std::error_code> asyncio::Pipe::from(const uv_file file) {
    auto pipe = std::make_unique<uv_pipe_t>();

    EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    EXPECT(uv::expected([&] {
        return uv_pipe_open(pipe.get(), file);
    }));

    return Pipe{uv::Handle{std::unique_ptr<uv_stream_t>{reinterpret_cast<uv_stream_t *>(pipe.release())}}};
}

std::expected<std::string, std::error_code> asyncio::Pipe::localAddress() const {
    std::size_t size = 1024;
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
        return std::unexpected(result.error());

    address.resize(size);

    EXPECT(uv::expected([&] {
        return uv_pipe_getsockname(reinterpret_cast<const uv_pipe_t *>(mStream.raw()), address.data(), &size);
    }));

    assert(address.size() == size);
    return address;
}

std::expected<std::string, std::error_code> asyncio::Pipe::remoteAddress() const {
    std::size_t size = 1024;
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
        return std::unexpected(result.error());

    address.resize(size);

    EXPECT(uv::expected([&] {
        return uv_pipe_getpeername(reinterpret_cast<const uv_pipe_t *>(mStream.raw()), address.data(), &size);
    }));

    assert(address.size() == size);
    return address;
}

std::expected<void, std::error_code> asyncio::Pipe::chmod(const int mode) {
    EXPECT(uv::expected([&] {
        return uv_pipe_chmod(reinterpret_cast<uv_pipe_t *>(mStream.raw()), mode);
    }));
    return {};
}

std::expected<std::array<asyncio::Pipe, 2>, std::error_code> asyncio::pipe() {
    std::array<uv_file, 2> fds = {};

    EXPECT(uv::expected([&] {
        return uv_pipe(fds.data(), UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE);
    }));

    DEFER(
        for (const auto &fd: fds) {
            if (fd == -1)
                continue;

            uv_fs_t request;
            uv_fs_close(nullptr, &request, fd, nullptr);
        }
    );

    auto reader = Pipe::from(fds[0]);
    EXPECT(reader);
    fds[0] = -1;

    auto writer = Pipe::from(fds[1]);
    EXPECT(writer);
    fds[0] = -1;

    return std::array{*std::move(reader), *std::move(writer)};
}

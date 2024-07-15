#include <asyncio/fs.h>
#include <asyncio/thread.h>

#ifdef _WIN32
#include <zero/os/nt/error.h>
#else
#include <unistd.h>
#include <zero/os/unix/error.h>
#endif

asyncio::fs::File::File(const uv_file file): mFile(file), mEventLoop(getEventLoop()) {
}

asyncio::fs::File::File(File &&rhs) noexcept
    : mFile(std::exchange(rhs.mFile, -1)), mEventLoop(std::move(rhs.mEventLoop)) {
}

asyncio::fs::File &asyncio::fs::File::operator=(File &&rhs) noexcept {
    mFile = std::exchange(rhs.mFile, -1);
    mEventLoop = std::move(rhs.mEventLoop);
    return *this;
}

asyncio::fs::File::~File() {
    if (mFile == -1)
        return;

    uv_fs_t request;
    uv_fs_close(nullptr, &request, mFile, nullptr);
    uv_fs_req_cleanup(&request);
}

asyncio::FileDescriptor asyncio::fs::File::fd() const {
    return uv_get_osfhandle(mFile);
}

asyncio::task::Task<std::size_t, std::error_code> asyncio::fs::File::read(const std::span<std::byte> data) {
    Promise<std::size_t, std::error_code> promise;
    uv_fs_t request = {.data = &promise};

    CO_EXPECT(uv::expected([&] {
        uv_buf_t buffer;

        buffer.base = reinterpret_cast<char *>(data.data());
        buffer.len = static_cast<decltype(uv_buf_t::len)>(data.size());

        return uv_fs_read(
            mEventLoop->raw(),
            &request,
            mFile,
            &buffer,
            1,
            -1,
            [](uv_fs_t *req) {
                const auto p = static_cast<Promise<std::size_t, std::error_code> *>(req->data);

                if (req->result < 0) {
                    p->reject(static_cast<uv::Error>(req->result));
                    return;
                }

                p->resolve(req->result);
            }
        );
    }));
    DEFER(uv_fs_req_cleanup(&request));

    co_return co_await promise.getFuture();
}

asyncio::task::Task<std::size_t, std::error_code> asyncio::fs::File::write(const std::span<const std::byte> data) {
    Promise<std::size_t, std::error_code> promise(mEventLoop);
    uv_fs_t request = {.data = &promise};

    CO_EXPECT(uv::expected([&] {
        uv_buf_t buffer;

        buffer.base = reinterpret_cast<char *>(const_cast<std::byte *>(data.data()));
        buffer.len = static_cast<decltype(uv_buf_t::len)>(data.size());

        return uv_fs_write(
            mEventLoop->raw(),
            &request,
            mFile,
            &buffer,
            1,
            -1,
            [](uv_fs_t *req) {
                const auto p = static_cast<Promise<std::size_t, std::error_code> *>(req->data);

                if (req->result < 0) {
                    p->reject(static_cast<uv::Error>(req->result));
                    return;
                }

                p->resolve(req->result);
            }
        );
    }));
    DEFER(uv_fs_req_cleanup(&request));

    co_return co_await promise.getFuture();
}

asyncio::task::Task<void, std::error_code> asyncio::fs::File::close() {
    Promise<void, std::error_code> promise(mEventLoop);
    uv_fs_t request = {.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_fs_close(
            mEventLoop->raw(),
            &request,
            mFile,
            [](uv_fs_t *req) {
                const auto p = static_cast<Promise<void, std::error_code> *>(req->data);

                if (req->result < 0) {
                    p->reject(static_cast<uv::Error>(req->result));
                    return;
                }

                p->resolve();
            }
        );
    }));
    DEFER(uv_fs_req_cleanup(&request));

    mFile = -1;
    co_return co_await promise.getFuture();
}

asyncio::task::Task<std::uint64_t, std::error_code>
asyncio::fs::File::seek(const std::int64_t offset, const Whence whence) {
    co_return co_await toThreadPool([&]() -> std::expected<std::uint64_t, std::error_code> {
#ifdef _WIN32
        LARGE_INTEGER pos;

        EXPECT(zero::os::nt::expected([&] {
            return SetFilePointerEx(
                uv_get_osfhandle(mFile),
                LARGE_INTEGER{.QuadPart = offset},
                &pos,
                whence == Whence::BEGIN ? FILE_BEGIN : whence == Whence::CURRENT ? FILE_CURRENT : FILE_END
            );
        }));

        return pos.QuadPart;
#else
        const auto pos = zero::os::unix::expected([&] {
#ifdef _LARGEFILE64_SOURCE
            return lseek64(
#else
            return lseek(
#endif
                mFile,
                offset,
                whence == Whence::BEGIN ? SEEK_SET : whence == Whence::CURRENT ? SEEK_CUR : SEEK_END
            );
        });
        EXPECT(pos);

        return *pos;
#endif
    }).transformError([](const ToThreadPoolError error) {
        return make_error_code(error);
    }).andThen([](const auto &result) -> std::expected<std::uint64_t, std::error_code> {
        if (!result)
            return std::unexpected(result.error());

        return *result;
    });
}

asyncio::task::Task<asyncio::fs::File, std::error_code>
asyncio::fs::open(const std::filesystem::path path, const int flags, const int mode) {
    Promise<uv_file, std::error_code> promise;

    uv_fs_t request = {.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_fs_open(
            getEventLoop()->raw(),
            &request,
            path.string().c_str(),
            flags,
            mode,
            [](uv_fs_t *req) {
                const auto p = static_cast<Promise<uv_file, std::error_code> *>(req->data);

                if (req->result < 0) {
                    p->reject(static_cast<uv::Error>(req->result));
                    return;
                }

                p->resolve(static_cast<uv_file>(req->result));
            }
        );
    }));
    DEFER(uv_fs_req_cleanup(&request));

    const auto file = co_await promise.getFuture();
    CO_EXPECT(file);

    co_return File{*file};
}

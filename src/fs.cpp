#include <asyncio/fs.h>

#ifdef _WIN32
#include <zero/os/windows/error.h>
#else
#include <unistd.h>
#include <zero/os/unix/error.h>
#endif

asyncio::fs::File::File(const uv_file file): mFile(file), mEventLoop{getEventLoop()} {
}

asyncio::fs::File::File(File &&rhs) noexcept
    : mFile{std::exchange(rhs.mFile, -1)}, mEventLoop{std::move(rhs.mEventLoop)} {
}

asyncio::fs::File &asyncio::fs::File::operator=(File &&rhs) noexcept {
    mFile = std::exchange(rhs.mFile, -1);
    mEventLoop = std::move(rhs.mEventLoop);
    return *this;
}

asyncio::fs::File::~File() {
    if (mFile == -1)
        return;

    uv_fs_t request{};
    uv_fs_close(nullptr, &request, mFile, nullptr);
    uv_fs_req_cleanup(&request);
}

asyncio::FileDescriptor asyncio::fs::File::fd() const {
    return uv_get_osfhandle(mFile);
}

asyncio::task::Task<std::size_t, std::error_code> asyncio::fs::File::read(const std::span<std::byte> data) {
    Promise<std::size_t, std::error_code> promise;
    uv_fs_t request{.data = &promise};

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
            [](auto *req) {
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
    uv_fs_t request{.data = &promise};

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
            [](auto *req) {
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
    uv_fs_t request{.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_fs_close(
            mEventLoop->raw(),
            &request,
            mFile,
            [](auto *req) {
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
        LARGE_INTEGER pos{};

        EXPECT(zero::os::windows::expected([&] {
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
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<asyncio::fs::File, std::error_code>
asyncio::fs::open(const std::filesystem::path path, const int flags, const int mode) {
    Promise<uv_file, std::error_code> promise;

    uv_fs_t request{.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_fs_open(
            getEventLoop()->raw(),
            &request,
            path.string().c_str(),
            flags,
            mode,
            [](auto *req) {
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

asyncio::task::Task<std::vector<std::byte>, std::error_code> asyncio::fs::read(std::filesystem::path path) {
    co_return co_await open(std::move(path), O_RDONLY).andThen(&IReader::readAll);
}

asyncio::task::Task<std::string, std::error_code> asyncio::fs::readString(std::filesystem::path path) {
    co_return co_await open(std::move(path), O_RDONLY)
                       .andThen(&IReader::readAll)
                       .transform([](const auto &content) {
                           return std::string{reinterpret_cast<const char *>(content.data()), content.size()};
                       });
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::write(std::filesystem::path path, const std::span<const std::byte> content) {
    auto file = co_await open(std::move(path), O_WRONLY | O_CREAT | O_TRUNC);
    CO_EXPECT(file);
    co_return co_await file->writeAll(content);
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::write(std::filesystem::path path, const std::string_view content) {
    auto file = co_await open(std::move(path), O_WRONLY | O_CREAT | O_TRUNC);
    CO_EXPECT(file);
    co_return co_await file->writeAll(std::as_bytes(std::span{content}));
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::absolute(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::absolute(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::canonical(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::canonical(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::path, std::error_code>
asyncio::fs::weaklyCanonical(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::weaklyCanonical(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::relative(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::relative(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::path, std::error_code>
asyncio::fs::relative(const std::filesystem::path path, const std::filesystem::path base) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::relative(path, base);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::proximate(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::proximate(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::path, std::error_code>
asyncio::fs::proximate(const std::filesystem::path path, const std::filesystem::path base) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::proximate(path, base);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::copy(const std::filesystem::path from, const std::filesystem::path to) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::copy(from, to);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::copy(
    const std::filesystem::path from,
    const std::filesystem::path to,
    const std::filesystem::copy_options options
) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::copy(from, to, options);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code>
asyncio::fs::copyFile(const std::filesystem::path from, const std::filesystem::path to) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::copyFile(from, to);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code>
asyncio::fs::copyFile(
    const std::filesystem::path from,
    const std::filesystem::path to,
    const std::filesystem::copy_options options
) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::copyFile(from, to, options);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::copySymlink(const std::filesystem::path from, const std::filesystem::path to) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::copySymlink(from, to);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::createDirectory(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::createDirectory(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code>
asyncio::fs::createDirectory(const std::filesystem::path path, const std::filesystem::path existing) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::createDirectory(path, existing);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::createDirectories(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::createDirectories(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::createHardLink(const std::filesystem::path target, const std::filesystem::path link) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::createHardLink(target, link);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::createSymlink(const std::filesystem::path target, const std::filesystem::path link) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::createSymlink(target, link);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::createDirectorySymlink(const std::filesystem::path target, const std::filesystem::path link) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::createDirectorySymlink(target, link);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::currentPath() {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::currentPath();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<void, std::error_code> asyncio::fs::currentPath(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::currentPath(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::exists(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::exists(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code>
asyncio::fs::equivalent(const std::filesystem::path p1, const std::filesystem::path p2) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::equivalent(p1, p2);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::uintmax_t, std::error_code> asyncio::fs::fileSize(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::fileSize(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::uintmax_t, std::error_code> asyncio::fs::hardLinkCount(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::hardLinkCount(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::file_time_type, std::error_code>
asyncio::fs::lastWriteTime(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::lastWriteTime(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::lastWriteTime(const std::filesystem::path path, const std::filesystem::file_time_type time) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::lastWriteTime(path, time);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::permissions(
    const std::filesystem::path path,
    const std::filesystem::perms perms,
    const std::filesystem::perm_options opts
) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::permissions(path, perms, opts);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::readSymlink(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::readSymlink(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::remove(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::remove(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::uintmax_t, std::error_code> asyncio::fs::removeAll(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::removeAll(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::rename(const std::filesystem::path from, const std::filesystem::path to) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::rename(from, to);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::resizeFile(const std::filesystem::path path, const std::uintmax_t size) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::resizeFile(path, size);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::space_info, std::error_code> asyncio::fs::space(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::space(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::file_status, std::error_code>
asyncio::fs::status(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::status(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::file_status, std::error_code>
asyncio::fs::symlinkStatus(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::symlinkStatus(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::temporaryDirectory() {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::temporaryDirectory();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isBlockFile(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::isBlockFile(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isCharacterFile(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::isCharacterFile(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isDirectory(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::isDirectory(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isEmpty(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::isEmpty(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isFIFO(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::isFIFO(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isOther(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::isOther(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isRegularFile(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::isRegularFile(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isSocket(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::isSocket(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isSymlink(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return zero::filesystem::isSymlink(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::fs::DirectoryEntry::DirectoryEntry(zero::filesystem::DirectoryEntry entry) : mEntry(std::move(entry)) {
}

asyncio::task::Task<void, std::error_code> asyncio::fs::DirectoryEntry::assign(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return mEntry.assign(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::DirectoryEntry::replaceFilename(const std::filesystem::path path) {
    co_return co_await toThreadPool([&] {
        return mEntry.replaceFilename(path);
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<void, std::error_code> asyncio::fs::DirectoryEntry::refresh() {
    co_return co_await toThreadPool([this] {
        return mEntry.refresh();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

const std::filesystem::path &asyncio::fs::DirectoryEntry::path() const {
    return mEntry.path();
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::exists() const {
    co_return co_await toThreadPool([this] {
        return mEntry.exists();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isBlockFile() const {
    co_return co_await toThreadPool([this] {
        return mEntry.isBlockFile();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isCharacterFile() const {
    co_return co_await toThreadPool([this] {
        return mEntry.isCharacterFile();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isDirectory() const {
    co_return co_await toThreadPool([this] {
        return mEntry.isDirectory();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isFIFO() const {
    co_return co_await toThreadPool([this] {
        return mEntry.isFIFO();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isOther() const {
    co_return co_await toThreadPool([this] {
        return mEntry.isOther();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isRegularFile() const {
    co_return co_await toThreadPool([this] {
        return mEntry.isRegularFile();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isSocket() const {
    co_return co_await toThreadPool([this] {
        return mEntry.isSocket();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isSymlink() const {
    co_return co_await toThreadPool([this] {
        return mEntry.isSymlink();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::uintmax_t, std::error_code> asyncio::fs::DirectoryEntry::fileSize() const {
    co_return co_await toThreadPool([this] {
        return mEntry.fileSize();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::uintmax_t, std::error_code> asyncio::fs::DirectoryEntry::hardLinkCount() const {
    co_return co_await toThreadPool([this] {
        return mEntry.hardLinkCount();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::file_time_type, std::error_code>
asyncio::fs::DirectoryEntry::lastWriteTime() const {
    co_return co_await toThreadPool([this] {
        return mEntry.lastWriteTime();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::file_status, std::error_code> asyncio::fs::DirectoryEntry::status() const {
    co_return co_await toThreadPool([this] {
        return mEntry.status();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<std::filesystem::file_status, std::error_code> asyncio::fs::DirectoryEntry::symlinkStatus() const {
    co_return co_await toThreadPool([this] {
        return mEntry.symlinkStatus();
    }).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<asyncio::fs::Asynchronous<std::filesystem::directory_iterator>, std::error_code>
asyncio::fs::readDirectory(const std::filesystem::path &path) {
    co_return co_await toThreadPool(
        [&]() -> std::expected<Asynchronous<std::filesystem::directory_iterator>, std::error_code> {
            std::error_code ec;
            std::filesystem::directory_iterator it{path, ec};

            if (ec)
                return std::unexpected{ec};

            return Asynchronous{std::move(it)};
        }
    ).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

asyncio::task::Task<asyncio::fs::Asynchronous<std::filesystem::recursive_directory_iterator>, std::error_code>
asyncio::fs::walkDirectory(const std::filesystem::path &path) {
    co_return co_await toThreadPool(
        [&]() -> std::expected<Asynchronous<std::filesystem::recursive_directory_iterator>, std::error_code> {
            std::error_code ec;
            std::filesystem::recursive_directory_iterator it{path, ec};

            if (ec)
                return std::unexpected{ec};

            return Asynchronous{std::move(it)};
        }
    ).transformError(make_error_code).andThen([](auto result) {
        return result;
    });
}

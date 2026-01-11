#include <asyncio/fs.h>
#include <asyncio/thread.h>
#include <zero/utility.h>

#ifdef _WIN32
#include <zero/os/windows/error.h>
#else
#include <unistd.h>
#include <zero/os/unix/error.h>
#endif

asyncio::fs::File::File(const uv_file file) : mFile{file}, mEventLoop{getEventLoop()} {
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

    zero::error::guard(uv::expected([&] {
        return uv_fs_close(nullptr, &request, mFile, nullptr);
    }));

    uv_fs_req_cleanup(&request);
}

asyncio::FileDescriptor asyncio::fs::File::fd() const {
    return uv_get_osfhandle(mFile);
}

asyncio::task::Task<std::size_t, std::error_code> asyncio::fs::File::read(const std::span<std::byte> data) {
    Promise<std::size_t, std::error_code> promise;
    uv_fs_t request{.data = &promise};

    Z_CO_EXPECT(uv::expected([&] {
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
    Z_DEFER(uv_fs_req_cleanup(&request));

    co_return co_await promise.getFuture();
}

asyncio::task::Task<std::size_t, std::error_code> asyncio::fs::File::write(const std::span<const std::byte> data) {
    Promise<std::size_t, std::error_code> promise{mEventLoop};
    uv_fs_t request{.data = &promise};

    Z_CO_EXPECT(uv::expected([&] {
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
    Z_DEFER(uv_fs_req_cleanup(&request));

    co_return co_await promise.getFuture();
}

asyncio::task::Task<void, std::error_code> asyncio::fs::File::close() {
    Promise<void, std::error_code> promise{mEventLoop};
    uv_fs_t request{.data = &promise};

    Z_CO_EXPECT(uv::expected([&] {
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
    Z_DEFER(uv_fs_req_cleanup(&request));

    mFile = -1;
    co_return co_await promise.getFuture();
}

asyncio::task::Task<std::uint64_t, std::error_code>
asyncio::fs::File::seek(const std::int64_t offset, const Whence whence) {
    co_return zero::flattenWith<std::error_code>(
        co_await toThreadPool([&]() -> std::expected<std::uint64_t, std::error_code> {
#ifdef _WIN32
            LARGE_INTEGER pos{};

            Z_EXPECT(zero::os::windows::expected([&] {
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
            Z_EXPECT(pos);

            return *pos;
#endif
        })
    );
}

asyncio::task::Task<asyncio::fs::File, std::error_code>
asyncio::fs::open(const std::filesystem::path path, const int flags, const int mode) {
    Promise<uv_file, std::error_code> promise;

    uv_fs_t request{.data = &promise};

    Z_CO_EXPECT(uv::expected([&] {
        return uv_fs_open(
            getEventLoop()->raw(),
            &request,
            zero::filesystem::stringify(path).c_str(),
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
    Z_DEFER(uv_fs_req_cleanup(&request));

    const auto file = co_await promise.getFuture();
    Z_CO_EXPECT(file);

    co_return File{*file};
}

asyncio::task::Task<std::vector<std::byte>, std::error_code> asyncio::fs::read(std::filesystem::path path) {
    return open(std::move(path), O_RDONLY).andThen(&IReader::readAll);
}

asyncio::task::Task<std::string, std::error_code> asyncio::fs::readString(std::filesystem::path path) {
    auto file = co_await open(std::move(path), O_RDONLY);
    Z_CO_EXPECT(file);

    StringWriter writer;
    Z_CO_EXPECT(co_await copy(*file, writer));

    co_return *std::move(writer);
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::write(std::filesystem::path path, const std::span<const std::byte> content) {
    auto file = co_await open(std::move(path), O_WRONLY | O_CREAT | O_TRUNC);
    Z_CO_EXPECT(file);
    co_return co_await file->writeAll(content);
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::write(std::filesystem::path path, const std::string content) {
    auto file = co_await open(std::move(path), O_WRONLY | O_CREAT | O_TRUNC);
    Z_CO_EXPECT(file);
    co_return co_await file->writeAll(std::as_bytes(std::span{content}));
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::absolute(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::absolute(path);
    }));
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::canonical(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::canonical(path);
    }));
}

asyncio::task::Task<std::filesystem::path, std::error_code>
asyncio::fs::weaklyCanonical(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::weaklyCanonical(path);
    }));
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::relative(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::relative(path);
    }));
}

asyncio::task::Task<std::filesystem::path, std::error_code>
asyncio::fs::relative(const std::filesystem::path path, const std::filesystem::path base) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::relative(path, base);
    }));
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::proximate(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::proximate(path);
    }));
}

asyncio::task::Task<std::filesystem::path, std::error_code>
asyncio::fs::proximate(const std::filesystem::path path, const std::filesystem::path base) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::proximate(path, base);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::copy(const std::filesystem::path from, const std::filesystem::path to) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::copy(from, to);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::copy(
    const std::filesystem::path from,
    const std::filesystem::path to,
    const std::filesystem::copy_options options
) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::copy(from, to, options);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::copyFile(const std::filesystem::path from, const std::filesystem::path to) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::copyFile(from, to);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::copyFile(
    const std::filesystem::path from,
    const std::filesystem::path to,
    const std::filesystem::copy_options options
) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::copyFile(from, to, options);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::copySymlink(const std::filesystem::path from, const std::filesystem::path to) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::copySymlink(from, to);
    }));
}

asyncio::task::Task<void, std::error_code> asyncio::fs::createDirectory(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::createDirectory(path);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::createDirectory(const std::filesystem::path path, const std::filesystem::path existing) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::createDirectory(path, existing);
    }));
}

asyncio::task::Task<void, std::error_code> asyncio::fs::createDirectories(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::createDirectories(path);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::createHardLink(const std::filesystem::path target, const std::filesystem::path link) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::createHardLink(target, link);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::createSymlink(const std::filesystem::path target, const std::filesystem::path link) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::createSymlink(target, link);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::createDirectorySymlink(const std::filesystem::path target, const std::filesystem::path link) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::createDirectorySymlink(target, link);
    }));
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::currentPath() {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::currentPath();
    }));
}

asyncio::task::Task<void, std::error_code> asyncio::fs::currentPath(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::currentPath(path);
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::exists(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::exists(path);
    }));
}

asyncio::task::Task<bool, std::error_code>
asyncio::fs::equivalent(const std::filesystem::path p1, const std::filesystem::path p2) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::equivalent(p1, p2);
    }));
}

asyncio::task::Task<std::uintmax_t, std::error_code> asyncio::fs::fileSize(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::fileSize(path);
    }));
}

asyncio::task::Task<std::uintmax_t, std::error_code> asyncio::fs::hardLinkCount(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::hardLinkCount(path);
    }));
}

asyncio::task::Task<std::filesystem::file_time_type, std::error_code>
asyncio::fs::lastWriteTime(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::lastWriteTime(path);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::lastWriteTime(const std::filesystem::path path, const std::filesystem::file_time_type time) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::lastWriteTime(path, time);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::permissions(
    const std::filesystem::path path,
    const std::filesystem::perms perms,
    const std::filesystem::perm_options opts
) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::permissions(path, perms, opts);
    }));
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::readSymlink(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::readSymlink(path);
    }));
}

asyncio::task::Task<void, std::error_code> asyncio::fs::remove(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::remove(path);
    }));
}

asyncio::task::Task<std::uintmax_t, std::error_code> asyncio::fs::removeAll(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::removeAll(path);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::rename(const std::filesystem::path from, const std::filesystem::path to) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::rename(from, to);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::resizeFile(const std::filesystem::path path, const std::uintmax_t size) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::resizeFile(path, size);
    }));
}

asyncio::task::Task<std::filesystem::space_info, std::error_code> asyncio::fs::space(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::space(path);
    }));
}

asyncio::task::Task<std::filesystem::file_status, std::error_code>
asyncio::fs::status(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::status(path);
    }));
}

asyncio::task::Task<std::filesystem::file_status, std::error_code>
asyncio::fs::symlinkStatus(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::symlinkStatus(path);
    }));
}

asyncio::task::Task<std::filesystem::path, std::error_code> asyncio::fs::temporaryDirectory() {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::temporaryDirectory();
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isBlockFile(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::isBlockFile(path);
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isCharacterFile(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::isCharacterFile(path);
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isDirectory(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::isDirectory(path);
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isEmpty(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::isEmpty(path);
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isFIFO(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::isFIFO(path);
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isOther(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::isOther(path);
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isRegularFile(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::isRegularFile(path);
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isSocket(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::isSocket(path);
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::isSymlink(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return zero::filesystem::isSymlink(path);
    }));
}

asyncio::fs::DirectoryEntry::DirectoryEntry(zero::filesystem::DirectoryEntry entry) : mEntry{std::move(entry)} {
}

asyncio::task::Task<void, std::error_code> asyncio::fs::DirectoryEntry::assign(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return mEntry.assign(path);
    }));
}

asyncio::task::Task<void, std::error_code>
asyncio::fs::DirectoryEntry::replaceFilename(const std::filesystem::path path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([&] {
        return mEntry.replaceFilename(path);
    }));
}

asyncio::task::Task<void, std::error_code> asyncio::fs::DirectoryEntry::refresh() {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.refresh();
    }));
}

const std::filesystem::path &asyncio::fs::DirectoryEntry::path() const {
    return mEntry.path();
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::exists() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.exists();
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isBlockFile() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.isBlockFile();
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isCharacterFile() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.isCharacterFile();
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isDirectory() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.isDirectory();
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isFIFO() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.isFIFO();
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isOther() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.isOther();
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isRegularFile() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.isRegularFile();
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isSocket() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.isSocket();
    }));
}

asyncio::task::Task<bool, std::error_code> asyncio::fs::DirectoryEntry::isSymlink() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.isSymlink();
    }));
}

asyncio::task::Task<std::uintmax_t, std::error_code> asyncio::fs::DirectoryEntry::fileSize() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.fileSize();
    }));
}

asyncio::task::Task<std::uintmax_t, std::error_code> asyncio::fs::DirectoryEntry::hardLinkCount() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.hardLinkCount();
    }));
}

asyncio::task::Task<std::filesystem::file_time_type, std::error_code>
asyncio::fs::DirectoryEntry::lastWriteTime() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.lastWriteTime();
    }));
}

asyncio::task::Task<std::filesystem::file_status, std::error_code> asyncio::fs::DirectoryEntry::status() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.status();
    }));
}

asyncio::task::Task<std::filesystem::file_status, std::error_code> asyncio::fs::DirectoryEntry::symlinkStatus() const {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool([this] {
        return mEntry.symlinkStatus();
    }));
}

template<typename T>
asyncio::fs::Asynchronous<T>::Asynchronous(T it) : mIterator{std::move(it)}, mStarted{false} {
}

template<typename T>
asyncio::task::Task<std::optional<asyncio::fs::DirectoryEntry>, std::error_code> asyncio::fs::Asynchronous<T>::next() {
    if (mIterator == std::default_sentinel)
        co_return std::nullopt;

    if (!mStarted) {
        mStarted = true;
        co_return DirectoryEntry{zero::filesystem::DirectoryEntry{*mIterator}};
    }

    Z_CO_EXPECT(zero::flattenWith<std::error_code>(
        co_await toThreadPool([this]() -> std::expected<void, std::error_code> {
            std::error_code ec;
            mIterator.increment(ec);

            if (ec)
                return std::unexpected{ec};

            return {};
        })
    ));

    if (mIterator == std::default_sentinel)
        co_return std::nullopt;

    co_return DirectoryEntry{zero::filesystem::DirectoryEntry{*mIterator}};
}

asyncio::task::Task<asyncio::fs::Asynchronous<std::filesystem::directory_iterator>, std::error_code>
asyncio::fs::readDirectory(const std::filesystem::path &path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool(
        [&]() -> std::expected<Asynchronous<std::filesystem::directory_iterator>, std::error_code> {
            std::error_code ec;
            std::filesystem::directory_iterator it{path, ec};

            if (ec)
                return std::unexpected{ec};

            return Asynchronous{std::move(it)};
        }
    ));
}

asyncio::task::Task<asyncio::fs::Asynchronous<std::filesystem::recursive_directory_iterator>, std::error_code>
asyncio::fs::walkDirectory(const std::filesystem::path &path) {
    co_return zero::flattenWith<std::error_code>(co_await toThreadPool(
        [&]() -> std::expected<Asynchronous<std::filesystem::recursive_directory_iterator>, std::error_code> {
            std::error_code ec;
            std::filesystem::recursive_directory_iterator it{path, ec};

            if (ec)
                return std::unexpected{ec};

            return Asynchronous{std::move(it)};
        }
    ));
}

template class asyncio::fs::Asynchronous<std::filesystem::directory_iterator>;
template class asyncio::fs::Asynchronous<std::filesystem::recursive_directory_iterator>;

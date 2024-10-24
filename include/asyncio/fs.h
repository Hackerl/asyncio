#ifndef ASYNCIO_FS_H
#define ASYNCIO_FS_H

#include "io.h"
#include <asyncio/thread.h>
#include <zero/filesystem/std.h>

namespace asyncio::fs {
    class File final : public IFileDescriptor, public IReader, public IWriter, public ICloseable, public ISeekable {
    public:
        explicit File(uv_file file);
        File(File &&rhs) noexcept;
        File &operator=(File &&rhs) noexcept;
        ~File() override;

        [[nodiscard]] FileDescriptor fd() const override;

        task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;
        task::Task<std::uint64_t, std::error_code> seek(std::int64_t offset, Whence whence) override;
        task::Task<void, std::error_code> close() override;

    private:
        uv_file mFile;
        std::shared_ptr<EventLoop> mEventLoop;
    };

    task::Task<File, std::error_code> open(std::filesystem::path path, int flags, int mode = 0644);

    task::Task<std::vector<std::byte>, std::error_code> read(std::filesystem::path path);
    task::Task<std::string, std::error_code> readString(std::filesystem::path path);

    task::Task<void, std::error_code> write(std::filesystem::path path, std::span<const std::byte> content);
    task::Task<void, std::error_code> write(std::filesystem::path path, std::string_view content);

    task::Task<std::filesystem::path, std::error_code> absolute(std::filesystem::path path);
    task::Task<std::filesystem::path, std::error_code> canonical(std::filesystem::path path);
    task::Task<std::filesystem::path, std::error_code> weaklyCanonical(std::filesystem::path path);
    task::Task<std::filesystem::path, std::error_code> relative(std::filesystem::path path);

    task::Task<std::filesystem::path, std::error_code>
    relative(std::filesystem::path path, std::filesystem::path base);

    task::Task<std::filesystem::path, std::error_code> proximate(std::filesystem::path path);

    task::Task<std::filesystem::path, std::error_code>
    proximate(std::filesystem::path path, std::filesystem::path base);

    task::Task<void, std::error_code> copy(std::filesystem::path from, std::filesystem::path to);

    task::Task<void, std::error_code>
    copy(std::filesystem::path from, std::filesystem::path to, std::filesystem::copy_options options);

    task::Task<bool, std::error_code> copyFile(std::filesystem::path from, std::filesystem::path to);

    task::Task<bool, std::error_code>
    copyFile(std::filesystem::path from, std::filesystem::path to, std::filesystem::copy_options options);

    task::Task<void, std::error_code>
    copySymlink(std::filesystem::path from, std::filesystem::path to);

    task::Task<bool, std::error_code> createDirectory(std::filesystem::path path);

    task::Task<bool, std::error_code>
    createDirectory(std::filesystem::path path, std::filesystem::path existing);

    task::Task<bool, std::error_code> createDirectories(std::filesystem::path path);

    task::Task<void, std::error_code>
    createHardLink(std::filesystem::path target, std::filesystem::path link);

    task::Task<void, std::error_code>
    createSymlink(std::filesystem::path target, std::filesystem::path link);

    task::Task<void, std::error_code>
    createDirectorySymlink(std::filesystem::path target, std::filesystem::path link);

    task::Task<std::filesystem::path, std::error_code> currentPath();
    task::Task<void, std::error_code> currentPath(std::filesystem::path path);

    task::Task<bool, std::error_code> exists(std::filesystem::path path);

    task::Task<bool, std::error_code> equivalent(std::filesystem::path p1, std::filesystem::path p2);

    task::Task<std::uintmax_t, std::error_code> fileSize(std::filesystem::path path);
    task::Task<std::uintmax_t, std::error_code> hardLinkCount(std::filesystem::path path);

    task::Task<std::filesystem::file_time_type, std::error_code> lastWriteTime(std::filesystem::path path);

    task::Task<void, std::error_code>
    lastWriteTime(std::filesystem::path path, std::filesystem::file_time_type time);

    task::Task<void, std::error_code>
    permissions(
        std::filesystem::path path,
        std::filesystem::perms perms,
        std::filesystem::perm_options opts = std::filesystem::perm_options::replace
    );

    task::Task<std::filesystem::path, std::error_code> readSymlink(std::filesystem::path path);

    task::Task<bool, std::error_code> remove(std::filesystem::path path);
    task::Task<std::uintmax_t, std::error_code> removeAll(std::filesystem::path path);

    task::Task<void, std::error_code> rename(std::filesystem::path from, std::filesystem::path to);
    task::Task<void, std::error_code> resizeFile(std::filesystem::path path, std::uintmax_t size);
    task::Task<std::filesystem::space_info, std::error_code> space(std::filesystem::path path);
    task::Task<std::filesystem::file_status, std::error_code> status(std::filesystem::path path);
    task::Task<std::filesystem::file_status, std::error_code> symlinkStatus(std::filesystem::path path);
    task::Task<std::filesystem::path, std::error_code> temporaryDirectory();

    task::Task<bool, std::error_code> isBlockFile(std::filesystem::path path);
    task::Task<bool, std::error_code> isCharacterFile(std::filesystem::path path);
    task::Task<bool, std::error_code> isDirectory(std::filesystem::path path);
    task::Task<bool, std::error_code> isEmpty(std::filesystem::path path);
    task::Task<bool, std::error_code> isFIFO(std::filesystem::path path);
    task::Task<bool, std::error_code> isOther(std::filesystem::path path);
    task::Task<bool, std::error_code> isRegularFile(std::filesystem::path path);
    task::Task<bool, std::error_code> isSocket(std::filesystem::path path);
    task::Task<bool, std::error_code> isSymlink(std::filesystem::path path);

    class DirectoryEntry {
    public:
        explicit DirectoryEntry(zero::filesystem::DirectoryEntry entry);

        task::Task<void, std::error_code> assign(std::filesystem::path path);
        task::Task<void, std::error_code> replaceFilename(std::filesystem::path path);
        task::Task<void, std::error_code> refresh();

        [[nodiscard]] const std::filesystem::path &path() const;
        [[nodiscard]] task::Task<bool, std::error_code> exists() const;

        [[nodiscard]] task::Task<bool, std::error_code> isBlockFile() const;
        [[nodiscard]] task::Task<bool, std::error_code> isCharacterFile() const;
        [[nodiscard]] task::Task<bool, std::error_code> isDirectory() const;
        [[nodiscard]] task::Task<bool, std::error_code> isFIFO() const;
        [[nodiscard]] task::Task<bool, std::error_code> isOther() const;
        [[nodiscard]] task::Task<bool, std::error_code> isRegularFile() const;
        [[nodiscard]] task::Task<bool, std::error_code> isSocket() const;
        [[nodiscard]] task::Task<bool, std::error_code> isSymlink() const;

        [[nodiscard]] task::Task<std::uintmax_t, std::error_code> fileSize() const;
        [[nodiscard]] task::Task<std::uintmax_t, std::error_code> hardLinkCount() const;
        [[nodiscard]] task::Task<std::filesystem::file_time_type, std::error_code> lastWriteTime() const;
        [[nodiscard]] task::Task<std::filesystem::file_status, std::error_code> status() const;
        [[nodiscard]] task::Task<std::filesystem::file_status, std::error_code> symlinkStatus() const;

    private:
        zero::filesystem::DirectoryEntry mEntry;
    };

    template<typename T>
        requires (
            std::is_same_v<T, std::filesystem::directory_iterator> ||
            std::is_same_v<T, std::filesystem::recursive_directory_iterator>
        )
    class Asynchronous {
    public:
        explicit Asynchronous(T it) : mIterator{std::move(it)}, mStarted{false} {
        }

        task::Task<std::optional<DirectoryEntry>, std::error_code> next() {
            if (mIterator == std::default_sentinel)
                co_return std::nullopt;

            if (!mStarted) {
                mStarted = true;
                co_return DirectoryEntry{zero::filesystem::DirectoryEntry{*mIterator}};
            }

            CO_EXPECT(co_await toThreadPool([this]() -> std::expected<void, std::error_code> {
                std::error_code ec;
                mIterator.increment(ec);

                if (ec)
                    return std::unexpected{ec};

                return {};
            }).transformError(make_error_code).andThen([](auto result) {
                return result;
            }));

            if (mIterator == std::default_sentinel)
                co_return std::nullopt;

            co_return DirectoryEntry{zero::filesystem::DirectoryEntry{*mIterator}};
        }

    private:
        T mIterator;
        bool mStarted;
    };

    task::Task<Asynchronous<std::filesystem::directory_iterator>, std::error_code>
    readDirectory(const std::filesystem::path &path);

    task::Task<Asynchronous<std::filesystem::recursive_directory_iterator>, std::error_code>
    walkDirectory(const std::filesystem::path &path);
}

#endif //ASYNCIO_FS_H

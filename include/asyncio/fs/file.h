#ifndef ASYNCIO_FILE_H
#define ASYNCIO_FILE_H

#include <filesystem>
#include <asyncio/io.h>
#include <asyncio/event_loop.h>

namespace asyncio::fs {
    class File final : public virtual IStreamIO, public IFileDescriptor, public ISeekable,
                       public Reader, public Writer {
    public:
        File(std::shared_ptr<EventLoop> eventLoop, FileDescriptor fd, bool append);
        File(File &&rhs) noexcept;
        File &operator=(File &&rhs) noexcept;
        ~File() override;

        static std::expected<File, std::error_code> from(FileDescriptor fd, bool append = false);

        zero::async::coroutine::Task<void, std::error_code> close();
        zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        zero::async::coroutine::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        std::expected<std::uint64_t, std::error_code> seek(std::int64_t offset, Whence whence) override;
        std::expected<void, std::error_code> rewind() override;
        [[nodiscard]] std::expected<std::uint64_t, std::error_code> length() const override;
        [[nodiscard]] std::expected<std::uint64_t, std::error_code> position() const override;

        [[nodiscard]] FileDescriptor fd() const override;

    private:
        bool mAppend;
        FileDescriptor mFD;
        std::uint64_t mOffset;
        std::shared_ptr<EventLoop> mEventLoop;
    };

    std::expected<File, std::error_code> open(const std::filesystem::path &path);
    std::expected<File, std::error_code> open(const std::filesystem::path &path, int flags);
}

#endif //ASYNCIO_FILE_H

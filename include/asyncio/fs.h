#ifndef ASYNCIO_FS_H
#define ASYNCIO_FS_H

#include "io.h"
#include <filesystem>

namespace asyncio::fs {
    class File final : public IReader, public IWriter, public ICloseable, public ISeekable {
    public:
        explicit File(uv_file file);
        File(File &&rhs) noexcept;
        File &operator=(File &&rhs) noexcept;
        ~File() override;

        task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;
        task::Task<std::uint64_t, std::error_code> seek(std::int64_t offset, Whence whence) override;
        task::Task<void, std::error_code> close() override;

    private:
        uv_file mFile;
        std::shared_ptr<EventLoop> mEventLoop;
    };

    task::Task<File, std::error_code> open(std::filesystem::path path, int flags, int mode = 0644);
}

#endif //ASYNCIO_FS_H

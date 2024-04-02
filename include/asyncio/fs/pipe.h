#ifndef ASNYCIO_FS_PIPE_H
#define ASNYCIO_FS_PIPE_H

#include <asyncio/io.h>
#include <asyncio/ev/event.h>

namespace asyncio::fs {
    class Pipe final : public virtual IStreamIO, public IFileDescriptor, public Reader, public Writer {
    public:
#ifdef _WIN32
        explicit Pipe(FileDescriptor fd);
#else
        Pipe(FileDescriptor fd, std::array<std::optional<ev::Event>, 2> events);
#endif

        Pipe(Pipe &&rhs) noexcept;
        Pipe &operator=(Pipe &&rhs) noexcept;
        ~Pipe() override;

        static tl::expected<Pipe, std::error_code> from(FileDescriptor fd);

        zero::async::coroutine::Task<void, std::error_code> close() override;
        zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        zero::async::coroutine::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        [[nodiscard]] FileDescriptor fd() const override;

    private:
        FileDescriptor mFD;
#ifndef _WIN32
        std::array<std::optional<ev::Event>, 2> mEvents;
#endif
    };

    tl::expected<std::array<Pipe, 2>, std::error_code> pipe();
}

#endif //ASNYCIO_FS_PIPE_H

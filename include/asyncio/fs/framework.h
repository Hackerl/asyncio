#ifndef ASYNCIO_FRAMEWORK_H
#define ASYNCIO_FRAMEWORK_H

#include <asyncio/io.h>

#if __unix__ || __APPLE__
#include <aio.h>
#include <event.h>
#endif

namespace asyncio::fs {
    class IFramework : public zero::Interface {
    public:
        virtual zero::async::coroutine::Task<size_t, std::error_code>
        read(FileDescriptor fd, off_t offset, std::span<std::byte> data) = 0;

        virtual zero::async::coroutine::Task<size_t, std::error_code>
        write(FileDescriptor fd, off_t offset, std::span<const std::byte> data) = 0;
    };

#if __unix__ || __APPLE__
    class PosixAIO : public IFramework {
    public:
        explicit PosixAIO(std::unique_ptr<event, decltype(event_free) *> event);
        PosixAIO(PosixAIO && rhs) noexcept;
        ~PosixAIO() override;

    public:
        zero::async::coroutine::Task<size_t, std::error_code>
        read(FileDescriptor fd, off_t offset, std::span<std::byte> data) override;

        zero::async::coroutine::Task<size_t, std::error_code>
        write(FileDescriptor fd, off_t offset, std::span<const std::byte> data) override;

    private:
        void onSignal();

    private:
        std::unique_ptr<event, decltype(event_free) *> mEvent;
        std::list<std::pair<aiocb *, zero::async::promise::Promise<size_t, std::error_code>>> mPending;
    };

    tl::expected<PosixAIO, std::error_code> makePosixAIO();
#endif
}

#endif //ASYNCIO_FRAMEWORK_H

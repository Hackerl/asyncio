#ifndef ASYNCIO_FRAMEWORK_H
#define ASYNCIO_FRAMEWORK_H

#include <asyncio/io.h>
#include <asyncio/event_loop.h>

#if __unix__ || __APPLE__
#include <aio.h>
#include <event.h>
#endif

namespace asyncio::fs {
    class IFramework : public zero::Interface {
    public:
        virtual tl::expected<void, std::error_code> associate(FileDescriptor fd) = 0;

        virtual zero::async::coroutine::Task<std::size_t, std::error_code>
        read(FileDescriptor fd, std::streamoff offset, std::span<std::byte> data) = 0;

        virtual zero::async::coroutine::Task<std::size_t, std::error_code>
        write(FileDescriptor fd, std::streamoff offset, std::span<const std::byte> data) = 0;
    };

#if __unix__ || __APPLE__
    class PosixAIO final : public IFramework {
    public:
        PosixAIO(EventLoop *eventLoop, std::unique_ptr<event, decltype(event_free) *> event);
        PosixAIO(PosixAIO && rhs) noexcept;
        ~PosixAIO() override;

    private:
        void onSignal();

    public:
        tl::expected<void, std::error_code> associate(FileDescriptor fd) override;

        zero::async::coroutine::Task<std::size_t, std::error_code>
        read(FileDescriptor fd, std::streamoff offset, std::span<std::byte> data) override;

        zero::async::coroutine::Task<std::size_t, std::error_code>
        write(FileDescriptor fd, std::streamoff offset, std::span<const std::byte> data) override;

    private:
        EventLoop *mEventLoop;
        std::unique_ptr<event, decltype(event_free) *> mEvent;
        std::list<std::pair<aiocb *, zero::async::promise::Promise<std::size_t, std::error_code>>> mPending;
    };

    tl::expected<PosixAIO, std::error_code> makePosixAIO(EventLoop *eventLoop);
#endif

#ifdef _WIN32
    class IOCP final : public IFramework {
    public:
        IOCP(EventLoop *eventLoop, HANDLE handle);
        IOCP(IOCP &&rhs) noexcept;
        ~IOCP() override;

    private:
        void dispatch() const;

    public:
        tl::expected<void, std::error_code> associate(FileDescriptor fd) override;

        zero::async::coroutine::Task<std::size_t, std::error_code>
        read(FileDescriptor fd, std::streamoff offset, std::span<std::byte> data) override;

        zero::async::coroutine::Task<std::size_t, std::error_code>
        write(FileDescriptor fd, std::streamoff offset, std::span<const std::byte> data) override;

    private:
        HANDLE mHandle;
        EventLoop *mEventLoop;
        std::thread mThread;
    };

    tl::expected<IOCP, std::error_code> makeIOCP(EventLoop *eventLoop);
#endif
}

#endif //ASYNCIO_FRAMEWORK_H

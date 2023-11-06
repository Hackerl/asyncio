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

    public:
        virtual zero::async::coroutine::Task<size_t, std::error_code>
        read(FileDescriptor fd, std::streamoff offset, std::span<std::byte> data) = 0;

        virtual zero::async::coroutine::Task<size_t, std::error_code>
        write(FileDescriptor fd, std::streamoff offset, std::span<const std::byte> data) = 0;
    };

#if __unix__ || __APPLE__
    class PosixAIO : public IFramework {
    public:
        PosixAIO(EventLoop *eventLoop, std::unique_ptr<event, decltype(event_free) *> event);
        PosixAIO(PosixAIO && rhs) noexcept;
        ~PosixAIO() override;

    public:
        tl::expected<void, std::error_code> associate(FileDescriptor fd) override;

    public:
        zero::async::coroutine::Task<size_t, std::error_code>
        read(FileDescriptor fd, std::streamoff offset, std::span<std::byte> data) override;

        zero::async::coroutine::Task<size_t, std::error_code>
        write(FileDescriptor fd, std::streamoff offset, std::span<const std::byte> data) override;

    private:
        void onSignal();

    private:
        EventLoop *mEventLoop;
        std::unique_ptr<event, decltype(event_free) *> mEvent;
        std::list<std::pair<aiocb *, zero::async::promise::Promise<size_t, std::error_code>>> mPending;
    };

    tl::expected<PosixAIO, std::error_code> makePosixAIO(EventLoop *eventLoop);
#endif

#ifdef _WIN32
    class IOCP : public IFramework {
    public:
        IOCP(EventLoop * eventLoop, HANDLE handle);
        IOCP(IOCP && rhs) noexcept;
        ~IOCP() override;

    public:
        tl::expected<void, std::error_code> associate(FileDescriptor fd) override;

    public:
        zero::async::coroutine::Task<size_t, std::error_code>
        read(FileDescriptor fd, std::streamoff offset, std::span<std::byte> data) override;

        zero::async::coroutine::Task<size_t, std::error_code>
        write(FileDescriptor fd, std::streamoff offset, std::span<const std::byte> data) override;

    private:
        void dispatch();

    private:
        HANDLE mHandle;
        EventLoop *mEventLoop;
        std::thread mThread;
    };

    tl::expected<IOCP, std::error_code> makeIOCP(EventLoop *eventLoop);
#endif
}

#endif //ASYNCIO_FRAMEWORK_H

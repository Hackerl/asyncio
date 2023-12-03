#ifndef ASYNCIO_IOCP_H
#define ASYNCIO_IOCP_H

#include "framework.h"
#include <thread>

namespace asyncio::fs {
    class IOCP final : public IFramework {
    public:
        explicit IOCP(HANDLE handle);
        IOCP(IOCP &&rhs) noexcept;
        ~IOCP() override;

    private:
        void dispatch() const;

    public:
        tl::expected<void, std::error_code> associate(FileDescriptor fd) override;

        zero::async::coroutine::Task<std::size_t, std::error_code>
        read(
            std::shared_ptr<EventLoop> eventLoop,
            FileDescriptor fd,
            std::uint64_t offset,
            std::span<std::byte> data
        ) override;

        zero::async::coroutine::Task<std::size_t, std::error_code>
        write(
            std::shared_ptr<EventLoop> eventLoop,
            FileDescriptor fd,
            std::uint64_t offset,
            std::span<const std::byte> data
        ) override;

    private:
        HANDLE mHandle;
        std::thread mThread;
    };

    tl::expected<IOCP, std::error_code> makeIOCP();
}

#endif //ASYNCIO_IOCP_H

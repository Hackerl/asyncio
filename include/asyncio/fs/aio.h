#ifndef ASYNCIO_AIO_H
#define ASYNCIO_AIO_H

#include "framework.h"
#include <linux/aio_abi.h>
#include <event.h>

namespace asyncio::fs {
    class AIO final : public IFramework {
    public:
        explicit AIO(aio_context_t context, int efd, std::unique_ptr<event, decltype(event_free) *> event);
        AIO(AIO &&rhs) noexcept;
        AIO &operator=(AIO &&rhs) noexcept;
        ~AIO() override;

        static std::expected<AIO, std::error_code> make(event_base *base);

    private:
        void onEvent() const;

    public:
        std::expected<void, std::error_code> associate(FileDescriptor fd) override;

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
        int mEventFD;
        aio_context_t mContext;
        std::unique_ptr<event, decltype(event_free) *> mEvent;
    };
}

#endif //ASYNCIO_AIO_H

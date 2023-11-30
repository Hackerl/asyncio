#ifndef POSIX_H
#define POSIX_H

#include "framework.h"
#include <event.h>
#include <aio.h>

namespace asyncio::fs {
    class PosixAIO final : public IFramework {
        struct Request {
            aiocb *cb;
            std::shared_ptr<EventLoop> eventLoop;
            zero::async::promise::Promise<std::size_t, std::error_code> promise;

            bool operator==(const Request &rhs) const;
        };

    public:
        explicit PosixAIO(std::unique_ptr<event, decltype(event_free) *> event);
        PosixAIO(PosixAIO && rhs) noexcept;
        ~PosixAIO() override;

    private:
        void onSignal();

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
        std::list<Request> mPending;
        std::unique_ptr<event, decltype(event_free) *> mEvent;
    };

    tl::expected<PosixAIO, std::error_code> makePosixAIO(event_base *base);
}

#endif //POSIX_H

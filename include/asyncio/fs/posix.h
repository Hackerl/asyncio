#ifndef ASYNCIO_POSIX_H
#define ASYNCIO_POSIX_H

#include "framework.h"
#include <event.h>
#include <aio.h>
#include <asyncio/promise.h>

namespace asyncio::fs {
    class PosixAIO final : public IFramework {
        struct Request {
            aiocb *cb;
            Promise<std::size_t, std::error_code> promise;
        };

    public:
        DEFINE_ERROR_CODE(
            Error,
            "asyncio::fs::PosixAIO",
            ALL_DONE, "all requests had already been completed before the call",
            NOT_CANCELED, "at least one of the requests specified was not canceled because it was in progress"
        )

        explicit PosixAIO(std::unique_ptr<event, decltype(event_free) *> event);
        PosixAIO(PosixAIO &&rhs) noexcept;
        ~PosixAIO() override;

        static std::expected<PosixAIO, std::error_code> make(event_base *base);

    private:
        void onSignal();

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
        std::list<Request *> mPending;
        std::unique_ptr<event, decltype(event_free) *> mEvent;
    };

    DEFINE_MAKE_ERROR_CODE(PosixAIO::Error)
}

DECLARE_ERROR_CODE(asyncio::fs::PosixAIO::Error)

#endif //ASYNCIO_POSIX_H

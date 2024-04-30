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
        enum class Error {
            ALL_DONE = 1,
            NOT_CANCELED
        };

        class ErrorCategory final : public std::error_category {
        public:
            [[nodiscard]] const char *name() const noexcept override;
            [[nodiscard]] std::string message(int value) const override;
        };

        explicit PosixAIO(std::unique_ptr<event, decltype(event_free) *> event);
        PosixAIO(PosixAIO &&rhs) noexcept;
        ~PosixAIO() override;

        static tl::expected<PosixAIO, std::error_code> make(event_base *base);

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
        std::list<Request *> mPending;
        std::unique_ptr<event, decltype(event_free) *> mEvent;
    };

    std::error_code make_error_code(PosixAIO::Error e);
}

template<>
struct std::is_error_code_enum<asyncio::fs::PosixAIO::Error> : std::true_type {
};

#endif //ASYNCIO_POSIX_H

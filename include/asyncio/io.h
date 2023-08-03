#ifndef ASYNCIO_IO_H
#define ASYNCIO_IO_H

#include <span>
#include <chrono>
#include <zero/interface.h>
#include <zero/async/coroutine.h>

namespace asyncio {
    class IReader : public virtual zero::Interface {
    public:
        virtual zero::async::coroutine::Task<size_t, std::error_code> read(std::span<std::byte> data) = 0;
    };

    class IWriter : public virtual zero::Interface {
    public:
        virtual zero::async::coroutine::Task<void, std::error_code> write(std::span<const std::byte> data) = 0;
    };

    class IStreamIO : public virtual IReader, public virtual IWriter {
    public:
        virtual tl::expected<void, std::error_code> close() = 0;
    };

    class IDeadline : public virtual zero::Interface {
    public:
        virtual void setTimeout(std::chrono::milliseconds timeout) = 0;
        virtual void setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) = 0;
    };
}

#endif //ASYNCIO_IO_H

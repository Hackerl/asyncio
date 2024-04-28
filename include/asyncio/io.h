#ifndef ASYNCIO_IO_H
#define ASYNCIO_IO_H

#include <span>
#include <chrono>
#include <event2/util.h>
#include <zero/interface.h>
#include <zero/async/coroutine.h>

namespace asyncio {
    enum IOError {
        BROKEN_PIPE = 1,
        INVALID_ARGUMENT,
        TIMED_OUT,
        DEVICE_OR_RESOURCE_BUSY,
        NOT_SUPPORTED,
        OPERATION_NOT_SUPPORTED,
        FUNCTION_NOT_SUPPORTED,
        UNEXPECTED_EOF,
        BAD_FILE_DESCRIPTOR,
        NOT_ENOUGH_MEMORY,
        ADDRESS_FAMILY_NOT_SUPPORTED
    };

    class IOErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
        [[nodiscard]] std::error_condition default_error_condition(int value) const noexcept override;
    };

    std::error_code make_error_code(IOError e);

    constexpr auto INVALID_FILE_DESCRIPTOR = -1;
    constexpr auto DEFAULT_BUFFER_CAPACITY = 1024 * 1024;

    using FileDescriptor = evutil_socket_t;

    class IReader : public virtual zero::Interface {
    public:
        virtual zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> readExactly(std::span<std::byte> data) = 0;
        virtual zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> readAll() = 0;
    };

    class IWriter : public virtual zero::Interface {
    public:
        virtual zero::async::coroutine::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> writeAll(std::span<const std::byte> data) = 0;
    };

    class IStreamIO : public virtual IReader, public virtual IWriter {
    public:
        virtual zero::async::coroutine::Task<void, std::error_code> close() = 0;
    };

    class IFileDescriptor : public virtual zero::Interface {
    public:
        [[nodiscard]] virtual FileDescriptor fd() const = 0;
    };

    class IDeadline : public virtual zero::Interface {
    public:
        virtual tl::expected<void, std::error_code> setTimeout(std::chrono::milliseconds timeout) = 0;

        virtual tl::expected<void, std::error_code>
        setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) = 0;
    };

    class ISeekable : public virtual zero::Interface {
    public:
        enum Whence {
            BEGIN,
            CURRENT,
            END
        };

        virtual tl::expected<std::uint64_t, std::error_code> seek(std::int64_t offset, Whence whence) = 0;
        virtual tl::expected<void, std::error_code> rewind() = 0;
        [[nodiscard]] virtual tl::expected<std::uint64_t, std::error_code> length() const = 0;
        [[nodiscard]] virtual tl::expected<std::uint64_t, std::error_code> position() const = 0;
    };

    class IBuffered : public virtual zero::Interface {
    public:
        [[nodiscard]] virtual std::size_t capacity() const = 0;
    };

    class IBufReader : public virtual IReader, public virtual IBuffered {
    public:
        [[nodiscard]] virtual std::size_t available() const = 0;
        virtual zero::async::coroutine::Task<std::string, std::error_code> readLine() = 0;
        virtual zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> readUntil(std::byte byte) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> peek(std::span<std::byte> data) = 0;
    };

    class IBufWriter : public virtual IWriter, public virtual IBuffered {
    public:
        [[nodiscard]] virtual std::size_t pending() const = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> flush() = 0;
    };

    class IBuffer : public virtual IStreamIO, public IBufReader, public IBufWriter {
    };

    class Reader : public virtual IReader {
    public:
        zero::async::coroutine::Task<void, std::error_code> readExactly(std::span<std::byte> data) override;
        zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> readAll() override;
    };

    class Writer : public virtual IWriter {
    public:
        zero::async::coroutine::Task<void, std::error_code> writeAll(std::span<const std::byte> data) override;
    };

    zero::async::coroutine::Task<void, std::error_code> copy(IReader &reader, IWriter &writer);

    template<typename R, typename W>
        requires (std::derived_from<R, IReader> && std::derived_from<W, IWriter>)
    zero::async::coroutine::Task<void, std::error_code> copy(std::shared_ptr<R> reader, std::shared_ptr<W> writer) {
        co_return co_await copy(*reader, *writer);
    }

    template<typename T, typename U>
        requires (std::derived_from<T, IStreamIO> && std::derived_from<U, IStreamIO>)
    zero::async::coroutine::Task<void, std::error_code>
    copyBidirectional(std::shared_ptr<T> first, std::shared_ptr<U> second) {
        co_return co_await race(copy(first, second), copy(second, first));
    }

    template<typename T, typename U>
        requires (std::derived_from<T, IStreamIO> && std::derived_from<U, IStreamIO>)
    zero::async::coroutine::Task<void, std::error_code>
    copyBidirectional(T first, U second) {
        co_return co_await copyBidirectional(
            std::make_shared<T>(std::move(first)),
            std::make_shared<U>(std::move(second))
        );
    }
}

template<>
struct std::is_error_code_enum<asyncio::IOError> : std::true_type {
};

#endif //ASYNCIO_IO_H

#ifndef ASYNCIO_IO_H
#define ASYNCIO_IO_H

#include <uv.h>
#include <span>
#include <chrono>
#include <zero/interface.h>
#include <zero/async/coroutine.h>

namespace asyncio {
    DEFINE_ERROR_CODE_EX(
        IOError,
        "asyncio::io",
        BROKEN_PIPE, "broken pipe", std::errc::broken_pipe,
        INVALID_ARGUMENT, "invalid argument", std::errc::invalid_argument,
        DEVICE_OR_RESOURCE_BUSY, "device or resource busy", std::errc::device_or_resource_busy,
        FUNCTION_NOT_SUPPORTED, "function not supported", std::errc::function_not_supported,
        UNEXPECTED_EOF, "unexpected end of file", DEFAULT_ERROR_CONDITION,
        BAD_FILE_DESCRIPTOR, "bad file descriptor", std::errc::bad_file_descriptor,
        ADDRESS_FAMILY_NOT_SUPPORTED, "address family not supported", std::errc::address_family_not_supported
    )

    //constexpr auto INVALID_FILE_DESCRIPTOR = -1;
    //constexpr auto DEFAULT_BUFFER_CAPACITY = 1024 * 1024;

    using OSSocket = uv_os_sock_t;

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

    /*class IFileDescriptor : public virtual zero::Interface {
    public:
        [[nodiscard]] virtual FileDescriptor fd() const = 0;
    };*/

    class ISeekable : public virtual zero::Interface {
    public:
        enum class Whence {
            BEGIN,
            CURRENT,
            END
        };

        virtual std::expected<std::uint64_t, std::error_code> seek(std::int64_t offset, Whence whence) = 0;
        virtual std::expected<void, std::error_code> rewind() = 0;
        [[nodiscard]] virtual std::expected<std::uint64_t, std::error_code> length() const = 0;
        [[nodiscard]] virtual std::expected<std::uint64_t, std::error_code> position() const = 0;
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

DECLARE_ERROR_CODE(asyncio::IOError)

#endif //ASYNCIO_IO_H

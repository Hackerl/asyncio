#ifndef ASYNCIO_IO_H
#define ASYNCIO_IO_H

#include <uv.h>
#include <span>
#include <chrono>
#include <zero/interface.h>
#include <zero/async/coroutine.h>

namespace asyncio {
    DEFINE_ERROR_CONDITION(
        IOError,
        "asyncio::io",
        UNEXPECTED_EOF, "unexpected end of file"
    )

    //constexpr auto INVALID_FILE_DESCRIPTOR = -1;
    //constexpr auto DEFAULT_BUFFER_CAPACITY = 1024 * 1024;

    class ICloseable : public virtual zero::Interface {
    public:
        virtual zero::async::coroutine::Task<void, std::error_code> close() = 0;
    };

    class IReader : public virtual zero::Interface {
    public:
        DEFINE_ERROR_CODE_INNER_EX(
            Error,
            "asyncio::IReader",
            UNEXPECTED_EOF, "unexpected end of file", make_error_condition(IOError::UNEXPECTED_EOF)
        )

        virtual zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> readExactly(std::span<std::byte> data);
        virtual zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> readAll();
    };

    class IWriter : public virtual zero::Interface {
    public:
        virtual zero::async::coroutine::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> writeAll(std::span<const std::byte> data);
    };

    template<typename T>
    concept Reader = std::derived_from<T, IReader>;

    template<typename T>
    concept Writer = std::derived_from<T, IWriter>;

    template<typename T>
    concept StreamIO = Reader<T> && Writer<T>;

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

    class IBufReader : public virtual IReader {
    public:
        [[nodiscard]] virtual std::size_t available() const = 0;
        virtual zero::async::coroutine::Task<std::string, std::error_code> readLine() = 0;
        virtual zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> readUntil(std::byte byte) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> peek(std::span<std::byte> data) = 0;
    };

    class IBufWriter : public virtual IWriter {
    public:
        [[nodiscard]] virtual std::size_t pending() const = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> flush() = 0;
    };

    zero::async::coroutine::Task<void, std::error_code> copy(IReader &reader, IWriter &writer);

    template<Reader R, Writer W>
    zero::async::coroutine::Task<void, std::error_code> copy(std::shared_ptr<R> reader, std::shared_ptr<W> writer) {
        co_return co_await copy(*reader, *writer);
    }

    template<StreamIO T, StreamIO U>
    zero::async::coroutine::Task<void, std::error_code>
    copyBidirectional(std::shared_ptr<T> first, std::shared_ptr<U> second) {
        co_return co_await race(copy(first, second), copy(second, first));
    }

    template<StreamIO T, StreamIO U>
    zero::async::coroutine::Task<void, std::error_code>
    copyBidirectional(T first, U second) {
        co_return co_await copyBidirectional(
            std::make_shared<T>(std::move(first)),
            std::make_shared<U>(std::move(second))
        );
    }
}

DECLARE_ERROR_CONDITION(asyncio::IOError)
DECLARE_ERROR_CODE(asyncio::IReader::Error)

#endif //ASYNCIO_IO_H

#ifndef ASYNCIO_IO_H
#define ASYNCIO_IO_H

#include "task.h"
#include <span>
#include <chrono>
#include <zero/interface.h>

namespace asyncio {
    template<typename T, typename I>
    concept Trait = std::derived_from<T, I> ||
        std::is_convertible_v<std::remove_const_t<T>, std::shared_ptr<I>> ||
        std::is_convertible_v<std::remove_const_t<T>, std::unique_ptr<I>>;

    DEFINE_ERROR_CONDITION(
        IOError,
        "asyncio::io",
        UNEXPECTED_EOF, "unexpected end of file"
    )

    using FileDescriptor = uv_os_fd_t;

    class IFileDescriptor : public virtual zero::Interface {
    public:
        [[nodiscard]] virtual FileDescriptor fd() const = 0;
    };

    class ICloseable : public virtual zero::Interface {
    public:
        virtual task::Task<void, std::error_code> close() = 0;
    };

    class IReader : public virtual zero::Interface {
    public:
        DEFINE_ERROR_CODE_INNER_EX(
            ReadExactlyError,
            "asyncio::IReader",
            UNEXPECTED_EOF, "unexpected end of file", make_error_condition(IOError::UNEXPECTED_EOF)
        )

        virtual task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) = 0;
        virtual task::Task<void, std::error_code> readExactly(std::span<std::byte> data);
        virtual task::Task<std::vector<std::byte>, std::error_code> readAll();
    };

    class IWriter : public virtual zero::Interface {
    public:
        virtual task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) = 0;
        virtual task::Task<void, std::error_code> writeAll(std::span<const std::byte> data);
    };

    class ISeekable : public virtual zero::Interface {
    public:
        enum class Whence {
            BEGIN,
            CURRENT,
            END
        };

        virtual task::Task<std::uint64_t, std::error_code> seek(std::int64_t offset, Whence whence) = 0;
        virtual task::Task<void, std::error_code> rewind();
        virtual task::Task<std::uint64_t, std::error_code> length();
        virtual task::Task<std::uint64_t, std::error_code> position();
    };

    class IBufReader : public virtual IReader {
    public:
        [[nodiscard]] virtual std::size_t available() const = 0;
        virtual task::Task<std::string, std::error_code> readLine() = 0;
        virtual task::Task<std::vector<std::byte>, std::error_code> readUntil(std::byte byte) = 0;
        virtual task::Task<void, std::error_code> peek(std::span<std::byte> data) = 0;
    };

    class IBufWriter : public virtual IWriter {
    public:
        [[nodiscard]] virtual std::size_t pending() const = 0;
        virtual task::Task<void, std::error_code> flush() = 0;
    };

    task::Task<void, std::error_code> copy(Trait<IReader> auto &reader, Trait<IWriter> auto &writer) {
        std::expected<void, std::error_code> result;

        while (true) {
            if (co_await task::cancelled) {
                result = std::unexpected<std::error_code>(task::Error::CANCELLED);
                break;
            }

            std::array<std::byte, 10240> data; // NOLINT(*-pro-type-member-init)
            const auto n = co_await std::invoke(&IReader::read, reader, data);

            if (!n) {
                result = std::unexpected(n.error());
                break;
            }

            if (*n == 0)
                break;

            co_await task::lock;

            if (const auto res = co_await std::invoke(&IWriter::writeAll, writer, std::span{data.data(), *n}); !res) {
                result = std::unexpected(res.error());
                break;
            }

            co_await task::unlock;
        }

        co_return result;
    }

    template<typename T, typename U>
        requires (Trait<T, IReader> && Trait<T, IWriter> && Trait<U, IReader> && Trait<U, IWriter>)
    task::Task<void, std::error_code> copyBidirectional(T &first, U &second) {
        co_return co_await race(copy(first, second), copy(second, first));
    }
}

DECLARE_ERROR_CONDITION(asyncio::IOError)
DECLARE_ERROR_CODE(asyncio::IReader::ReadExactlyError)

#endif //ASYNCIO_IO_H

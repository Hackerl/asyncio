#ifndef ASYNCIO_IO_H
#define ASYNCIO_IO_H

#include "task.h"
#include <span>
#include <chrono>
#include <zero/interface.h>

namespace asyncio {
    template<typename T, typename I>
    concept Trait = std::is_convertible_v<std::remove_cvref_t<T>, std::shared_ptr<I>> || (
        std::derived_from<std::remove_cvref_t<T>, std::remove_const_t<I>> &&
        std::is_convertible_v<std::add_lvalue_reference_t<T>, I &>
    );

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

    task::Task<std::size_t, std::error_code> copy(Trait<IReader> auto &reader, Trait<IWriter> auto &writer) {
        std::size_t written{0};

        while (true) {
            if (co_await task::cancelled)
                co_return std::unexpected{task::Error::CANCELLED};

            std::array<std::byte, 20480> data; // NOLINT(*-pro-type-member-init)

            const auto n = co_await std::invoke(&IReader::read, reader, data);
            CO_EXPECT(n);

            if (*n == 0)
                break;

            co_await task::lock;
            CO_EXPECT(co_await std::invoke(&IWriter::writeAll, writer, std::span{data.data(), *n}));
            co_await task::unlock;

            written += *n;
        }

        co_return written;
    }

    class StringReader final : public IReader {
    public:
        explicit StringReader(std::string string);

        task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;

    private:
        std::string mString;
    };

    class StringWriter final : public IWriter {
    public:
        task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        template<typename Self>
        auto &&data(this Self &&self) {
            return std::forward<Self>(self).mString;
        }

        template<typename Self>
        auto &&operator*(this Self &&self) {
            return std::forward<Self>(self).mString;
        }

    private:
        std::string mString;
    };

    class BytesReader final : public IReader {
    public:
        explicit BytesReader(std::vector<std::byte> bytes);

        task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;

    private:
        std::vector<std::byte> mBytes;
    };

    class BytesWriter final : public IWriter {
    public:
        task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        template<typename Self>
        auto &&data(this Self &&self) {
            return std::forward<Self>(self).mBytes;
        }

        template<typename Self>
        auto &&operator*(this Self &&self) {
            return std::forward<Self>(self).mBytes;
        }

    private:
        std::vector<std::byte> mBytes;
    };
}

DECLARE_ERROR_CONDITION(asyncio::IOError)
DECLARE_ERROR_CODE(asyncio::IReader::ReadExactlyError)

#endif //ASYNCIO_IO_H

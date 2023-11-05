#ifndef ASYNCIO_IO_H
#define ASYNCIO_IO_H

#include <span>
#include <chrono>
#include <event2/util.h>
#include <asyncio/error.h>
#include <zero/interface.h>
#include <zero/async/coroutine.h>

namespace asyncio {
    constexpr auto INVALID_FILE_DESCRIPTOR = -1;
    using FileDescriptor = evutil_socket_t;

    class IReader : public virtual zero::Interface {
    public:
        virtual zero::async::coroutine::Task<size_t, std::error_code> read(std::span<std::byte> data) = 0;
    };

    class IWriter : public virtual zero::Interface {
    public:
        virtual zero::async::coroutine::Task<void, std::error_code> write(std::span<const std::byte> data) = 0;
    };

    class ICloseable : public virtual zero::Interface {
    public:
        virtual tl::expected<void, std::error_code> close() = 0;
    };

    class ICloseableReader : public virtual IReader, public virtual ICloseable {

    };

    class ICloseableWriter : public virtual IWriter, public virtual ICloseable {

    };

    class IStreamIO : public virtual ICloseableReader, public virtual ICloseableWriter {

    };

    class IFileDescriptor : public virtual zero::Interface {
    public:
        virtual FileDescriptor fd() = 0;
    };

    class IDeadline : public virtual zero::Interface {
    public:
        virtual void setTimeout(std::chrono::milliseconds timeout) = 0;
        virtual void setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) = 0;
    };

    template<typename R, typename W>
    requires (std::derived_from<R, IReader> && std::derived_from<W, IWriter>)
    zero::async::coroutine::Task<void, std::error_code> copy(R reader, W writer) {
        tl::expected<void, std::error_code> result;

        while (true) {
            std::byte data[10240];
            auto n = co_await reader.read(data);

            if (!n) {
                if (n.error() == Error::IO_EOF)
                    break;

                result = tl::unexpected(n.error());
                break;
            }

            auto res = co_await writer.write({data, *n});

            if (!res) {
                result = tl::unexpected(res.error());
                break;
            }
        }

        co_return result;
    }

    template<typename T>
    requires std::derived_from<T, IReader>
    zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> readAll(T reader) {
        tl::expected<std::vector<std::byte>, std::error_code> result;

        while (true) {
            std::byte data[10240];
            auto n = co_await reader.read(data);

            if (!n) {
                if (n.error() == Error::IO_EOF)
                    break;

                result = tl::unexpected(n.error());
                break;
            }

            result->insert(result->end(), data, data + *n);
        }

        co_return result;
    }

    template<typename T>
    requires std::derived_from<T, IReader>
    zero::async::coroutine::Task<void, std::error_code> readExactly(T reader, std::span<std::byte> data) {
        tl::expected<void, std::error_code> result;

        size_t offset = 0;

        while (offset < data.size()) {
            auto n = co_await reader.read(data.subspan(offset));

            if (!n) {
                result = tl::unexpected(n.error());
                break;
            }

            offset += *n;
        }

        co_return result;
    }

    zero::async::coroutine::Task<void, std::error_code>
    copy(std::shared_ptr<IReader> reader, std::shared_ptr<IWriter> writer);

    zero::async::coroutine::Task<std::vector<std::byte>, std::error_code>
    readAll(std::shared_ptr<IReader> reader);

    zero::async::coroutine::Task<void, std::error_code>
    readExactly(std::shared_ptr<IReader> reader, std::span<std::byte> data);
}

#endif //ASYNCIO_IO_H

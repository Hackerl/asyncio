#ifndef ASYNCIO_STREAM_H
#define ASYNCIO_STREAM_H

#include "io.h"
#include "sync/event.h"

namespace asyncio {
    namespace net {
        class TCPStream;
        class TCPListener;
#ifdef _WIN32
        class NamedPipeStream;
        class NamedPipeListener;
#else
        class UnixStream;
        class UnixListener;
#endif
    }

    class Stream : public IReader, public IWriter, public ICloseable {
    public:
        explicit Stream(uv::Handle<uv_stream_t> stream);
        static std::expected<std::array<Stream, 2>, std::error_code> pair();

    private:
        task::Task<void, std::error_code> shutdown();

    public:
        task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;
        task::Task<void, std::error_code> close() override;

        std::expected<std::size_t, std::error_code> tryWrite(std::span<const std::byte> data);

    protected:
        uv::Handle<uv_stream_t> mStream;

        friend class net::TCPStream;
#ifdef _WIN32
        friend class net::NamedPipeStream;
#else
        friend class net::UnixStream;
#endif
    };

    class Listener : public ICloseable {
        struct Core {
            uv::Handle<uv_stream_t> stream;
            sync::Event event;
            std::optional<std::error_code> ec;
        };

    public:
        explicit Listener(std::unique_ptr<Core> core);
        static std::expected<Listener, std::error_code> make(uv::Handle<uv_stream_t> stream);

        task::Task<void, std::error_code> accept(uv_stream_t *stream);
        task::Task<void, std::error_code> close() override;

    protected:
        std::unique_ptr<Core> mCore;

        friend class net::TCPListener;
#ifdef _WIN32
        friend class net::NamedPipeListener;
#else
        friend class net::UnixListener;
#endif
    };
}

#endif //ASYNCIO_STREAM_H

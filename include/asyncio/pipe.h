#ifndef ASYNCIO_PIPE_H
#define ASYNCIO_PIPE_H

#include "stream.h"

namespace asyncio {
    class Pipe final : public IFileDescriptor, public Stream {
    public:
        enum Mode {
            READABLE = UV_READABLE,
            WRITABLE = UV_WRITABLE,
        };

        explicit Pipe(uv::Handle<uv_stream_t> stream);
        static std::expected<Pipe, std::error_code> from(uv_file file);

    private:
        std::expected<void, std::error_code> chmod(int mode);

    public:
        [[nodiscard]] FileDescriptor fd() const override;

        [[nodiscard]] std::expected<std::string, std::error_code> localAddress() const;
        [[nodiscard]] std::expected<std::string, std::error_code> remoteAddress() const;

#ifdef _WIN32
        friend class net::NamedPipeStream;
#else
        friend class net::UnixStream;
#endif
    };

    std::expected<std::array<Pipe, 2>, std::error_code> pipe();
}

#endif //ASYNCIO_PIPE_H

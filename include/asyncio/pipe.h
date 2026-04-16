#ifndef ASYNCIO_PIPE_H
#define ASYNCIO_PIPE_H

#include "stream.h"

namespace asyncio {
    class Pipe : public IFileDescriptor, public Stream {
    public:
        explicit Pipe(uv::Handle<uv_stream_t> stream);
        static Pipe from(uv_file file);

        [[nodiscard]] FileDescriptor fd() const override;

        [[nodiscard]] std::expected<std::string, std::error_code> localAddress() const;
        [[nodiscard]] std::expected<std::string, std::error_code> remoteAddress() const;
    };

    class PipeListener final : public IFileDescriptor, public Listener {
    public:
        enum Mode {
            Readable = UV_READABLE,
            Writable = UV_WRITABLE,
        };

        explicit PipeListener(Listener listener);

        [[nodiscard]] FileDescriptor fd() const override;
        [[nodiscard]] std::expected<std::string, std::error_code> address() const;

        std::expected<void, std::error_code> chmod(int mode);
    };

    std::array<Pipe, 2> pipe();
}

#endif //ASYNCIO_PIPE_H

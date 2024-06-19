#ifndef ASYNCIO_STREAM_H
#define ASYNCIO_STREAM_H

#include "io.h"
#include "uv.h"
#include "sync/event.h"

namespace asyncio {
    class Stream : public IReader, public IWriter {
    public:
        explicit Stream(uv::Handle<uv_stream_t> stream);

        uv::Handle<uv_stream_t> &handle();
        [[nodiscard]] const uv::Handle<uv_stream_t> &handle() const;

        zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        zero::async::coroutine::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

    protected:
        uv::Handle<uv_stream_t> mStream;
    };

    class Listener {
        struct Context {
            uv::Handle<uv_stream_t> stream;
            sync::Event event;
            std::optional<std::error_code> ec;
        };

    public:
        explicit Listener(std::unique_ptr<Context> context);
        static std::expected<Listener, std::error_code> make(uv::Handle<uv_stream_t> stream);

        uv::Handle<uv_stream_t> &handle();
        [[nodiscard]] const uv::Handle<uv_stream_t> &handle() const;

        zero::async::coroutine::Task<void, std::error_code> accept(uv_stream_t *stream);

    private:
        std::unique_ptr<Context> mContext;
    };
}

#endif //ASYNCIO_STREAM_H

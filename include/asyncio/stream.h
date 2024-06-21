#ifndef ASYNCIO_STREAM_H
#define ASYNCIO_STREAM_H

#include "io.h"
#include "sync/event.h"

namespace asyncio {
    class Stream : public IReader, public IWriter, public ICloseable {
    public:
        explicit Stream(uv::Handle<uv_stream_t> stream);
        static std::expected<std::array<Stream, 2>, std::error_code> pair();

        uv::Handle<uv_stream_t> &handle();
        [[nodiscard]] const uv::Handle<uv_stream_t> &handle() const;

        task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;
        task::Task<void, std::error_code> close() override;

    protected:
        uv::Handle<uv_stream_t> mStream;
    };

    class Listener {
        struct Core {
            uv::Handle<uv_stream_t> stream;
            sync::Event event;
            std::optional<std::error_code> ec;
        };

    public:
        explicit Listener(std::unique_ptr<Core> core);
        static std::expected<Listener, std::error_code> make(uv::Handle<uv_stream_t> stream);

        uv::Handle<uv_stream_t> &handle();
        [[nodiscard]] const uv::Handle<uv_stream_t> &handle() const;

        task::Task<void, std::error_code> accept(uv_stream_t *stream);

    private:
        std::unique_ptr<Core> mCore;
    };

    std::expected<std::array<Stream, 2>, std::error_code> pair();
}

#endif //ASYNCIO_STREAM_H

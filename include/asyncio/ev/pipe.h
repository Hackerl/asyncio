#ifndef ASYNCIO_PIPE_H
#define ASYNCIO_PIPE_H

#include "buffer.h"
#include <array>

namespace asyncio::ev {
    class PairedBuffer final : public Buffer {
    public:
        PairedBuffer(std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev, std::size_t capacity);
        PairedBuffer(PairedBuffer &&) = default;
        ~PairedBuffer() override;

        zero::async::coroutine::Task<void, std::error_code> close() override;
    };

    tl::expected<std::array<PairedBuffer, 2>, std::error_code> pipe(std::size_t capacity = DEFAULT_BUFFER_CAPACITY);
}

#endif //ASYNCIO_PIPE_H

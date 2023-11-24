#ifndef ASYNCIO_PIPE_H
#define ASYNCIO_PIPE_H

#include "buffer.h"
#include <array>

namespace asyncio::ev {
    class PairedBuffer final : public Buffer {
    public:
        PairedBuffer(bufferevent *bev, std::size_t capacity, std::shared_ptr<std::error_code> ec);
        PairedBuffer(PairedBuffer &&) = default;
        ~PairedBuffer() override;

    private:
        [[nodiscard]] std::error_code getError() const override;

    public:
        zero::async::coroutine::Task<void, std::error_code> close() override;
        tl::expected<void, std::error_code> throws(const std::error_code &ec);

    private:
        std::shared_ptr<std::error_code> mErrorCode;
    };

    tl::expected<std::array<PairedBuffer, 2>, std::error_code> pipe(std::size_t capacity = DEFAULT_BUFFER_CAPACITY);
}

#endif //ASYNCIO_PIPE_H

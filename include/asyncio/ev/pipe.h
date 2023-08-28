#ifndef ASYNCIO_PIPE_H
#define ASYNCIO_PIPE_H

#include "buffer.h"
#include <array>

namespace asyncio::ev {
    class IPairedBuffer : public virtual IBuffer {
    public:
        virtual tl::expected<void, std::error_code> throws(const std::error_code &ec) = 0;
    };

    class PairedBuffer : public Buffer, public IPairedBuffer {
    public:
        PairedBuffer(bufferevent *bev, std::shared_ptr<std::error_code> ec);
        PairedBuffer(PairedBuffer &&rhs) = default;
        ~PairedBuffer() override;

    public:
        tl::expected<void, std::error_code> close() override;
        tl::expected<void, std::error_code> throws(const std::error_code &ec) override;

    private:
        std::error_code getError() override;

    private:
        std::shared_ptr<std::error_code> mErrorCode;
    };

    tl::expected<std::array<PairedBuffer, 2>, std::error_code> pipe();
}

#endif //ASYNCIO_PIPE_H

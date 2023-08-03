#ifndef ASYNCIO_PIPE_H
#define ASYNCIO_PIPE_H

#include "buffer.h"
#include <array>

namespace asyncio::ev {
    class IPairedBuffer : public virtual IBuffer {
    public:
        virtual void throws(const std::error_code &ec) = 0;
    };

    class PairedBuffer : public Buffer, public IPairedBuffer {
    public:
        PairedBuffer(bufferevent *bev, std::shared_ptr<std::error_code> ec);
        ~PairedBuffer() override;

    public:
        tl::expected<void, std::error_code> close() override;

    private:
        std::error_code getError() override;
        void throws(const std::error_code &ec) override;

    private:
        std::shared_ptr<std::error_code> mErrorCode;
    };

    std::array<std::shared_ptr<IPairedBuffer>, 2> pipe();
}

#endif //ASYNCIO_PIPE_H

#ifndef ASYNCIO_DGRAM_H
#define ASYNCIO_DGRAM_H

#include "net.h"
#include <asyncio/ev/event.h>

namespace asyncio::net::dgram {
    class Socket : public ISocket {
    private:
        Socket(evutil_socket_t fd, ev::Event events[2]);
        Socket(Socket &&rhs) noexcept;
        ~Socket() override;

    public:
        zero::async::coroutine::Task<size_t, std::error_code> read(std::span<std::byte> data) override;
        zero::async::coroutine::Task<void, std::error_code> write(std::span<const std::byte> data) override;
        tl::expected<void, std::error_code> close() override;

    public:
        void setTimeout(std::chrono::milliseconds timeout) override;
        void setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) override;

    public:
        tl::expected<Address, std::error_code> localAddress() override;
        tl::expected<Address, std::error_code> remoteAddress() override;

    public:
        evutil_socket_t fd() override;
        tl::expected<void, std::error_code> bind(const Address &address) override;
        zero::async::coroutine::Task<void, std::error_code> connect(const Address &address) override;

    private:
        bool mClosed;
        evutil_socket_t mFD;
        std::array<ev::Event, 2> mEvents;
        std::array<std::optional<std::chrono::milliseconds>, 2> mTimeouts;
    };
}
#endif //ASYNCIO_DGRAM_H

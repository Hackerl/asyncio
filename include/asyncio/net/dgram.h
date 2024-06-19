#ifndef ASYNCIO_DGRAM_H
#define ASYNCIO_DGRAM_H

#include "net.h"
#include <asyncio/uv.h>

namespace asyncio::net {
    class UDPSocket final : public ISocket, public ICloseable {
    public:
        explicit UDPSocket(uv::Handle<uv_udp_t> udp);

    private:
        static std::expected<UDPSocket, std::error_code> bind(const SocketAddress &address);
        static std::expected<UDPSocket, std::error_code> connect(const SocketAddress &address);

    public:
        static std::expected<UDPSocket, std::error_code> bind(const std::string &ip, unsigned short port);
        static std::expected<UDPSocket, std::error_code> bind(const IPv4Address &address);
        static std::expected<UDPSocket, std::error_code> bind(const IPv6Address &address);

        static zero::async::coroutine::Task<UDPSocket, std::error_code>
        connect(std::string host, unsigned short port);

        static std::expected<UDPSocket, std::error_code> connect(const IPv4Address &address);
        static std::expected<UDPSocket, std::error_code> connect(const IPv6Address &address);

        [[nodiscard]] std::expected<Address, std::error_code> localAddress() const override;
        [[nodiscard]] std::expected<Address, std::error_code> remoteAddress() const override;

        zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        zero::async::coroutine::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        zero::async::coroutine::Task<std::pair<std::size_t, Address>, std::error_code>
        readFrom(std::span<std::byte> data) override;

        zero::async::coroutine::Task<std::size_t, std::error_code>
        writeTo(std::span<const std::byte> data, Address address) override;

        zero::async::coroutine::Task<void, std::error_code> close() override;

    private:
        uv::Handle<uv_udp_t> mUDP;
    };
}
#endif //ASYNCIO_DGRAM_H

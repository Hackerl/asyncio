#ifndef ASYNCIO_DGRAM_H
#define ASYNCIO_DGRAM_H

#include "net.h"

namespace asyncio::net {
    class UDPSocket final : public ISocket {
    public:
        enum class Membership {
            JOIN_GROUP = UV_JOIN_GROUP,
            LEAVE_GROUP = UV_LEAVE_GROUP
        };

        explicit UDPSocket(uv::Handle<uv_udp_t> udp);

    private:
        static UDPSocket make();
        static std::expected<UDPSocket, std::error_code> bind(const SocketAddress &address);
        static std::expected<UDPSocket, std::error_code> connect(const SocketAddress &address);

    public:
        static std::expected<UDPSocket, std::error_code> from(uv_os_sock_t socket);

        static std::expected<UDPSocket, std::error_code> bind(const std::string &ip, std::uint16_t port);
        static std::expected<UDPSocket, std::error_code> bind(const IPv4Address &address);
        static std::expected<UDPSocket, std::error_code> bind(const IPv6Address &address);

        static task::Task<UDPSocket, std::error_code>
        connect(std::string host, std::uint16_t port);

        static std::expected<UDPSocket, std::error_code> connect(const IPv4Address &address);
        static std::expected<UDPSocket, std::error_code> connect(const IPv6Address &address);

        [[nodiscard]] FileDescriptor fd() const override;

        [[nodiscard]] std::expected<Address, std::error_code> localAddress() const override;
        [[nodiscard]] std::expected<Address, std::error_code> remoteAddress() const override;

        std::expected<void, std::error_code>
        setMembership(const std::string &multicastAddress, const std::string &interfaceAddress, Membership membership);

        std::expected<void, std::error_code>
        setSourceMembership(
            const std::string &multicastAddress,
            const std::string &interfaceAddress,
            const std::string &sourceAddress,
            Membership membership
        );

        std::expected<void, std::error_code> setMulticastLoop(bool on);
        std::expected<void, std::error_code> setMulticastTTL(int ttl);
        std::expected<void, std::error_code> setMulticastInterface(const std::string &interfaceAddress);
        std::expected<void, std::error_code> setBroadcast(bool on);
        std::expected<void, std::error_code> setTTL(int ttl);

        task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        task::Task<std::pair<std::size_t, Address>, std::error_code>
        readFrom(std::span<std::byte> data) override;

        task::Task<std::size_t, std::error_code>
        writeTo(std::span<const std::byte> data, Address address) override;

        task::Task<std::size_t, std::error_code>
        writeTo(std::span<const std::byte> data, std::string host, std::uint16_t port);

        task::Task<void, std::error_code> close() override;

    private:
        uv::Handle<uv_udp_t> mUDP;
    };
}
#endif //ASYNCIO_DGRAM_H

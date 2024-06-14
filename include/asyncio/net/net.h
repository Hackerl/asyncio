#ifndef ASYNCIO_NET_H
#define ASYNCIO_NET_H

#include <variant>
#include <asyncio/io.h>
#include <zero/os/net.h>

namespace asyncio::net {
    using IPv4 = zero::os::net::IPv4;
    using IPv6 = zero::os::net::IPv6;
    using IP = zero::os::net::IP;

    struct IPv4Address {
        unsigned short port;
        IPv4 ip;

        bool operator==(const IPv4Address &) const = default;
        static std::expected<IPv4Address, std::error_code> from(const std::string &ip, unsigned short port);
    };

    struct IPv6Address {
        unsigned short port;
        IPv6 ip;
        std::optional<std::string> zone;

        bool operator==(const IPv6Address &) const = default;

        static IPv6Address from(const IPv4Address &ipv4);
        static std::expected<IPv6Address, std::error_code> from(const std::string &ip, unsigned short port);
    };

    struct UnixAddress {
        std::string path;

        bool operator==(const UnixAddress &) const = default;
    };

    using Address = std::variant<IPv4Address, IPv6Address, UnixAddress>;
    using SocketAddress = std::pair<std::unique_ptr<sockaddr>, socklen_t>;

    template<typename T>
        requires (std::is_same_v<T, IPv4Address> || std::is_same_v<T, IPv6Address> || std::is_same_v<T, UnixAddress>)
    bool operator==(const Address &lhs, const T &rhs) {
        if constexpr (std::is_same_v<T, IPv4Address>) {
            return std::holds_alternative<IPv4Address>(lhs) && std::get<IPv4Address>(lhs) == rhs;
        }
        else if constexpr (std::is_same_v<T, IPv6Address>) {
            return std::holds_alternative<IPv6Address>(lhs) && std::get<IPv6Address>(lhs) == rhs;
        }
        else {
            return std::holds_alternative<UnixAddress>(lhs) && std::get<UnixAddress>(lhs) == rhs;
        }
    }

    std::expected<Address, std::error_code> addressFrom(const std::string &ip, unsigned short port);
    std::expected<Address, std::error_code> addressFrom(const sockaddr *addr, socklen_t length);

    std::expected<SocketAddress, std::error_code> socketAddressFrom(const Address &address);

    class IEndpoint : public virtual zero::Interface {
    public:
        [[nodiscard]] virtual std::expected<Address, std::error_code> localAddress() const = 0;
        [[nodiscard]] virtual std::expected<Address, std::error_code> remoteAddress() const = 0;
    };

    class ISocket : public virtual IStreamIO, public virtual IEndpoint {
    public:
        virtual zero::async::coroutine::Task<std::pair<std::size_t, Address>, std::error_code>
        readFrom(std::span<std::byte> data) = 0;

        virtual zero::async::coroutine::Task<std::size_t, std::error_code>
        writeTo(std::span<const std::byte> data, Address address) = 0;
    };
}

template<typename Char>
struct fmt::formatter<asyncio::net::IPv4Address, Char> {
    template<typename ParseContext>
    static constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template<typename FmtContext>
    static auto format(const asyncio::net::IPv4Address &address, FmtContext &ctx) {
        return fmt::format_to(ctx.out(), "{}:{}", zero::os::net::stringify(address.ip), address.port);
    }
};

template<typename Char>
struct fmt::formatter<asyncio::net::IPv6Address, Char> {
    template<typename ParseContext>
    static constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template<typename FmtContext>
    static auto format(const asyncio::net::IPv6Address &address, FmtContext &ctx) {
        if (!address.zone)
            return fmt::format_to(ctx.out(), "[{}]:{}", zero::os::net::stringify(address.ip), address.port);

        return fmt::format_to(
            ctx.out(),
            "[{}%{}]:{}",
            zero::os::net::stringify(address.ip), *address.zone, address.port
        );
    }
};

template<typename Char>
struct fmt::formatter<asyncio::net::UnixAddress, Char> {
    template<typename ParseContext>
    static constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template<typename FmtContext>
    static auto format(const asyncio::net::UnixAddress &address, FmtContext &ctx) {
        return std::ranges::copy(address.path, ctx.out()).out;
    }
};

#endif //ASYNCIO_NET_H

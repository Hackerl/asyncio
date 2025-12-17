#ifndef ASYNCIO_NET_H
#define ASYNCIO_NET_H

#include <asyncio/io.h>
#include <zero/os/net.h>

namespace asyncio::net {
    using IPv4 = zero::os::net::IPv4;
    using IPv6 = zero::os::net::IPv6;
    using IP = zero::os::net::IP;

    inline constexpr auto LOCALHOST_IPV4 = zero::os::net::LOCALHOST_IPV4;
    inline constexpr auto BROADCAST_IPV4 = zero::os::net::BROADCAST_IPV4;
    inline constexpr auto UNSPECIFIED_IPV4 = zero::os::net::UNSPECIFIED_IPV4;
    inline constexpr auto LOCALHOST_IPV6 = zero::os::net::LOCALHOST_IPV6;
    inline constexpr auto UNSPECIFIED_IPV6 = zero::os::net::UNSPECIFIED_IPV6;

    struct IPv4Address {
        IPv4 ip{};
        std::uint16_t port{};

        auto operator<=>(const IPv4Address &) const = default;
        static std::expected<IPv4Address, std::error_code> from(const std::string &ip, std::uint16_t port);
    };

    struct IPv6Address {
        IPv6 ip{};
        std::uint16_t port{};
        std::optional<std::string> zone;

        auto operator<=>(const IPv6Address &) const = default;

        static IPv6Address from(const IPv4Address &ipv4);
        static std::expected<IPv6Address, std::error_code> from(const std::string &ip, std::uint16_t port);
    };

    struct UnixAddress {
        std::string path;

        auto operator<=>(const UnixAddress &) const = default;
    };

    using IPAddress = std::variant<IPv4Address, IPv6Address>;
    using Address = std::variant<IPv4Address, IPv6Address, UnixAddress>;
    using SocketAddress = std::pair<std::unique_ptr<sockaddr, decltype(&std::free)>, socklen_t>;

    template<typename T>
        requires (std::is_same_v<T, IPv4Address> || std::is_same_v<T, IPv6Address>)
    bool operator==(const IPAddress &lhs, const T &rhs) {
        return std::visit(
            [&]<typename U>(const U &arg) {
                if constexpr (std::is_same_v<U, T>)
                    return arg == rhs;
                else
                    return false;
            },
            lhs
        );
    }

    template<typename T>
        requires (std::is_same_v<T, IPv4Address> || std::is_same_v<T, IPv6Address>)
    bool operator==(const T &lhs, const IPAddress &rhs) {
        return std::visit(
            [&]<typename U>(const U &arg) {
                if constexpr (std::is_same_v<U, T>)
                    return arg == lhs;
                else
                    return false;
            },
            rhs
        );
    }

    template<typename T>
        requires (std::is_same_v<T, IPv4Address> || std::is_same_v<T, IPv6Address> || std::is_same_v<T, UnixAddress>)
    bool operator==(const Address &lhs, const T &rhs) {
        return std::visit(
            [&]<typename U>(const U &arg) {
                if constexpr (std::is_same_v<U, T>)
                    return arg == rhs;
                else
                    return false;
            },
            lhs
        );
    }

    template<typename T>
        requires (std::is_same_v<T, IPv4Address> || std::is_same_v<T, IPv6Address> || std::is_same_v<T, UnixAddress>)
    bool operator==(const T &lhs, const Address &rhs) {
        return std::visit(
            [&]<typename U>(const U &arg) {
                if constexpr (std::is_same_v<U, T>)
                    return arg == lhs;
                else
                    return false;
            },
            rhs
        );
    }

    Z_DEFINE_ERROR_CODE_EX(
        ParseAddressError,
        "asyncio::net::addressFrom",
        INVALID_ARGUMENT, "Invalid argument", std::errc::invalid_argument,
        ADDRESS_FAMILY_NOT_SUPPORTED, "Address family not supported", std::errc::address_family_not_supported
    )

    std::expected<IPAddress, std::error_code> ipAddressFrom(const std::string &ip, std::uint16_t port);
    std::expected<Address, std::error_code> addressFrom(const sockaddr *addr, socklen_t length);

    Z_DEFINE_ERROR_CODE_EX(
        ConvertToSocketAddressError,
        "asyncio::net::socketAddressFrom",
        INVALID_ARGUMENT, "Invalid argument", std::errc::invalid_argument,
        ADDRESS_FAMILY_NOT_SUPPORTED, "Address family not supported", std::errc::address_family_not_supported
    )

    std::expected<SocketAddress, std::error_code> socketAddressFrom(const Address &address);

    class IEndpoint : public virtual zero::Interface {
    public:
        [[nodiscard]] virtual std::expected<Address, std::error_code> localAddress() const = 0;
        [[nodiscard]] virtual std::expected<Address, std::error_code> remoteAddress() const = 0;
    };

    class ISocket : public IFileDescriptor, public IReader, public IWriter, public ICloseable, public IEndpoint {
    public:
        virtual task::Task<std::pair<std::size_t, Address>, std::error_code>
        readFrom(std::span<std::byte> data) = 0;

        virtual task::Task<std::size_t, std::error_code>
        writeTo(std::span<const std::byte> data, Address address) = 0;
    };

    template<typename T, typename U>
        requires (
            zero::detail::Trait<T, IReader> &&
            zero::detail::Trait<T, IWriter> &&
            zero::detail::Trait<T, IHalfCloseable> &&
            zero::detail::Trait<U, IReader> &&
            zero::detail::Trait<U, IWriter> &&
            zero::detail::Trait<U, IHalfCloseable>
        )
    task::Task<std::array<std::size_t, 2>, std::error_code>
    copyBidirectional(T &first, U &second) {
        co_return co_await all(
            task::spawn([&]() -> task::Task<std::size_t, std::error_code> {
                const auto result = co_await copy(first, second);
                Z_CO_EXPECT(result);
                Z_CO_EXPECT(co_await std::invoke(&IHalfCloseable::shutdown, second));
                co_return *result;
            }),
            task::spawn([&]() -> task::Task<std::size_t, std::error_code> {
                const auto result = co_await copy(second, first);
                Z_CO_EXPECT(result);
                Z_CO_EXPECT(co_await std::invoke(&IHalfCloseable::shutdown, first));
                co_return *result;
            })
        );
    }
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
        return fmt::format_to(ctx.out(), "unix://{}", address.path);
    }
};

Z_DECLARE_ERROR_CODES(asyncio::net::ParseAddressError, asyncio::net::ConvertToSocketAddressError)

#endif //ASYNCIO_NET_H

#include <asyncio/net/net.h>

#ifdef _WIN32
#include <netioapi.h>
#include <zero/strings/strings.h>
#elif defined(__linux__)
#include <net/if.h>
#include <netinet/in.h>
#elif defined(__APPLE__)
#include <net/if.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <sys/un.h>
#endif

std::expected<asyncio::net::IPv4Address, std::error_code>
asyncio::net::IPv4Address::from(const std::string &ip, const unsigned short port) {
    IPv4 ipv4{};

    EXPECT(uv::expected([&] {
        return uv_inet_pton(AF_INET, ip.c_str(), ipv4.data());
    }));

    return IPv4Address{port, ipv4};
}

asyncio::net::IPv6Address asyncio::net::IPv6Address::from(const IPv4Address &ipv4) {
    IPv6Address address;

    address.port = ipv4.port;
    address.ip[10] = std::byte{255};
    address.ip[11] = std::byte{255};

    std::memcpy(address.ip.data() + 12, ipv4.ip.data(), 4);

    return address;
}

std::expected<asyncio::net::IPv6Address, std::error_code>
asyncio::net::IPv6Address::from(const std::string &ip, const unsigned short port) {
    IPv6 ipv6{};
    const auto pos = ip.find_last_of('%');

    EXPECT(uv::expected([&] {
        return uv_inet_pton(AF_INET6, ip.substr(0, pos).c_str(), ipv6.data());
    }));

    if (pos == std::string::npos)
        return IPv6Address{port, ipv6};

    return IPv6Address{port, ipv6, ip.substr(pos + 1)};
}

std::expected<asyncio::net::Address, std::error_code>
asyncio::net::addressFrom(const std::string &ip, const unsigned short port) {
    if (auto result = IPv6Address::from(ip, port))
        return result;

    return IPv4Address::from(ip, port);
}

std::expected<asyncio::net::Address, std::error_code>
asyncio::net::addressFrom(const sockaddr *addr, const socklen_t length) {
    switch (addr->sa_family) {
    case AF_INET: {
        if (length != 0 && length != sizeof(sockaddr_in))
            return std::unexpected{ParseAddressError::INVALID_ARGUMENT};

        const auto address = reinterpret_cast<const sockaddr_in *>(addr);

        IPv4Address ipv4;

        ipv4.port = ntohs(address->sin_port);
        std::memcpy(ipv4.ip.data(), &address->sin_addr, sizeof(in_addr));

        return ipv4;
    }

    case AF_INET6: {
        if (length != 0 && length != sizeof(sockaddr_in6))
            return std::unexpected{ParseAddressError::INVALID_ARGUMENT};

        const auto address = reinterpret_cast<const sockaddr_in6 *>(addr);

        IPv6Address ipv6;

        ipv6.port = ntohs(address->sin6_port);
        std::memcpy(ipv6.ip.data(), &address->sin6_addr, sizeof(in6_addr));

        if (address->sin6_scope_id == 0)
            return ipv6;

        std::array<char, IF_NAMESIZE + 1> name{};

        if (!if_indextoname(address->sin6_scope_id, name.data()))
            return std::unexpected{std::error_code{errno, std::generic_category()}};

        ipv6.zone = name.data();
        return ipv6;
    }

#if defined(__unix__) || defined(__APPLE__)
    case AF_UNIX: {
        if (length < sizeof(sa_family_t))
            return std::unexpected{ParseAddressError::INVALID_ARGUMENT};

        if (length == sizeof(sa_family_t))
            return UnixAddress{};

        const auto address = reinterpret_cast<const sockaddr_un *>(addr);

        if (address->sun_path[0] == '\0')
            return UnixAddress{
                fmt::format("@{}", std::string_view{address->sun_path + 1, length - sizeof(sa_family_t) - 1})
            };

        return UnixAddress{address->sun_path};
    }
#endif

    default:
        return std::unexpected{ParseAddressError::ADDRESS_FAMILY_NOT_SUPPORTED};
    }
}

std::expected<asyncio::net::SocketAddress, std::error_code>
asyncio::net::socketAddressFrom(const Address &address) {
    return std::visit(
        []<typename T>(const T &arg) -> std::expected<SocketAddress, std::error_code> {
            auto addr = std::unique_ptr<sockaddr, decltype(&std::free)>(
                static_cast<sockaddr *>(std::malloc(sizeof(sockaddr_storage))),
                std::free
            );

            if (!addr)
                return std::unexpected{std::error_code{errno, std::generic_category()}};

            std::memset(addr.get(), 0, sizeof(sockaddr_storage));

            if constexpr (std::is_same_v<T, IPv4Address>) {
                const auto ptr = reinterpret_cast<sockaddr_in *>(addr.get());

                ptr->sin_family = AF_INET;
                ptr->sin_port = htons(arg.port);
                std::memcpy(&ptr->sin_addr, arg.ip.data(), sizeof(in_addr));

                return SocketAddress{std::move(addr), static_cast<socklen_t>(sizeof(sockaddr_in))};
            }
            else if constexpr (std::is_same_v<T, IPv6Address>) {
                const auto ptr = reinterpret_cast<sockaddr_in6 *>(addr.get());

                ptr->sin6_family = AF_INET6;
                ptr->sin6_port = htons(arg.port);
                std::memcpy(&ptr->sin6_addr, arg.ip.data(), sizeof(in6_addr));

                if (!arg.zone)
                    return SocketAddress{std::move(addr), static_cast<socklen_t>(sizeof(sockaddr_in6))};

                const auto index = if_nametoindex(arg.zone->c_str());

                if (!index)
                    return std::unexpected{std::error_code{errno, std::generic_category()}};

                ptr->sin6_scope_id = index;
                return SocketAddress{std::move(addr), static_cast<socklen_t>(sizeof(sockaddr_in6))};
            }
#if defined(__unix__) || defined(__APPLE__)
            else if constexpr (std::is_same_v<T, UnixAddress>) {
                const auto ptr = reinterpret_cast<sockaddr_un *>(addr.get());
                const auto &path = arg.path;

                if (path.empty() || path.length() - (path.front() == '@' ? 1 : 0) >= sizeof(sockaddr_un::sun_path))
                    return std::unexpected{ConvertToSocketAddressError::INVALID_ARGUMENT};

                ptr->sun_family = AF_UNIX;
                std::memcpy(ptr->sun_path, path.c_str(), path.length());

                socklen_t length = sizeof(sa_family_t) + path.length() + 1;

                if (path.front() == '@') {
                    --length;
                    ptr->sun_path[0] = '\0';
                }

                return SocketAddress{std::move(addr), length};
            }
#endif
            else {
                return std::unexpected{ConvertToSocketAddressError::ADDRESS_FAMILY_NOT_SUPPORTED};
            }
        },
        address
    );
}

DEFINE_ERROR_CATEGORY_INSTANCES(asyncio::net::ParseAddressError, asyncio::net::ConvertToSocketAddressError)

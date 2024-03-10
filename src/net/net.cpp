#include <asyncio/net/net.h>
#include <cstring>

#ifdef _WIN32
#include <netioapi.h>
#elif __linux__
#include <net/if.h>
#include <netinet/in.h>
#elif __APPLE__
#include <net/if.h>
#endif

#if __unix__ || __APPLE__
#include <sys/un.h>
#endif

tl::expected<asyncio::net::IPv4Address, std::error_code>
asyncio::net::IPv4Address::from(const std::string &ip, const unsigned short port) {
    IPv4 ipv4 = {};

    if (evutil_inet_pton(AF_INET, ip.c_str(), ipv4.data()) != 1)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return IPv4Address{port, ipv4};
}

asyncio::net::IPv6Address asyncio::net::IPv6Address::from(const IPv4Address &ipv4) {
    IPv6Address address = {};

    address.port = ipv4.port;
    address.ip[10] = std::byte{255};
    address.ip[11] = std::byte{255};

    memcpy(address.ip.data() + 12, ipv4.ip.data(), 4);

    return address;
}

tl::expected<asyncio::net::IPv6Address, std::error_code>
asyncio::net::IPv6Address::from(const std::string &ip, const unsigned short port) {
    unsigned int index = 0;
    IPv6 ipv6 = {};

    if (evutil_inet_pton_scope(AF_INET6, ip.c_str(), ipv6.data(), &index) != 1)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    if (!index)
        return IPv6Address{port, ipv6};

    char name[IF_NAMESIZE];

    if (!if_indextoname(index, name))
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return IPv6Address{port, ipv6, name};
}

tl::expected<asyncio::net::Address, std::error_code>
asyncio::net::addressFrom(const std::string &ip, const unsigned short port) {
    if (auto result = IPv6Address::from(ip, port); result)
        return result;

    return IPv4Address::from(ip, port);
}

tl::expected<asyncio::net::Address, std::error_code>
asyncio::net::addressFrom(const FileDescriptor fd, const bool peer) {
    sockaddr_storage storage = {};
    socklen_t length = sizeof(sockaddr_storage);

    if ((peer ? getpeername : getsockname)(fd, reinterpret_cast<sockaddr *>(&storage), &length) < 0)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return addressFrom(reinterpret_cast<const sockaddr *>(&storage), length);
}

tl::expected<asyncio::net::Address, std::error_code>
asyncio::net::addressFrom(const sockaddr *addr, const socklen_t length) {
    tl::expected<Address, std::error_code> result;

    switch (addr->sa_family) {
    case AF_INET: {
        if (length != sizeof(sockaddr_in)) {
            result = tl::unexpected(make_error_code(std::errc::invalid_argument));
            break;
        }

        const auto address = reinterpret_cast<const sockaddr_in *>(addr);

        IPv4Address ipv4 = {};

        ipv4.port = ntohs(address->sin_port);
        memcpy(ipv4.ip.data(), &address->sin_addr, sizeof(in_addr));

        result = ipv4;
        break;
    }

    case AF_INET6: {
        if (length != sizeof(sockaddr_in6)) {
            result = tl::unexpected(make_error_code(std::errc::invalid_argument));
            break;
        }

        const auto address = reinterpret_cast<const sockaddr_in6 *>(addr);

        IPv6Address ipv6 = {};

        ipv6.port = ntohs(address->sin6_port);
        memcpy(ipv6.ip.data(), &address->sin6_addr, sizeof(in6_addr));

        if (address->sin6_scope_id == 0) {
            result = ipv6;
            break;
        }

        char name[IF_NAMESIZE];

        if (!if_indextoname(address->sin6_scope_id, name)) {
            result = tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
            break;
        }

        ipv6.zone = name;
        result = ipv6;

        break;
    }

#if __unix__ || __APPLE__
    case AF_UNIX: {
        if (length < sizeof(sa_family_t)) {
            result = tl::unexpected(make_error_code(std::errc::invalid_argument));
            break;
        }

        if (length == sizeof(sa_family_t)) {
            result = UnixAddress{};
            break;
        }

        const auto address = reinterpret_cast<const sockaddr_un *>(addr);

        if (address->sun_path[0] == '\0') {
            result = UnixAddress{
                fmt::format("@{}", std::string_view{address->sun_path + 1, length - sizeof(sa_family_t) - 1})
            };
            break;
        }

        result = UnixAddress{address->sun_path};
        break;
    }
#endif

    default:
        result = tl::unexpected(make_error_code(std::errc::address_family_not_supported));
        break;
    }

    return result;
}

tl::expected<std::pair<asyncio::net::SocketAddress, socklen_t>, std::error_code>
asyncio::net::socketAddressFrom(const Address &address) {
    return std::visit(
        []<typename T>(const T &arg) -> tl::expected<std::pair<SocketAddress, socklen_t>, std::error_code> {
            void *storage = malloc(sizeof(sockaddr_storage));

            if (!storage)
                return tl::unexpected(std::error_code(errno, std::system_category()));

            memset(storage, 0, sizeof(sockaddr_storage));

            std::pair result = {SocketAddress{static_cast<sockaddr *>(storage), free}, socklen_t{0}};

            if constexpr (std::is_same_v<T, IPv4Address>) {
                const auto ptr = static_cast<sockaddr_in *>(storage);

                ptr->sin_family = AF_INET;
                ptr->sin_port = htons(arg.port);
                memcpy(&ptr->sin_addr, arg.ip.data(), sizeof(in_addr));

                result.second = sizeof(sockaddr_in);
                return result;
            }
            else if constexpr (std::is_same_v<T, IPv6Address>) {
                const auto ptr = static_cast<sockaddr_in6 *>(storage);

                ptr->sin6_family = AF_INET6;
                ptr->sin6_port = htons(arg.port);
                memcpy(&ptr->sin6_addr, arg.ip.data(), sizeof(in6_addr));

                result.second = sizeof(sockaddr_in6);

                if (!arg.zone)
                    return result;

                const unsigned int index = if_nametoindex(arg.zone->c_str());

                if (!index)
                    return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

                ptr->sin6_scope_id = index;
                return result;
            }
#if __unix__ || __APPLE__
            else if constexpr (std::is_same_v<T, UnixAddress>) {
                const auto ptr = static_cast<sockaddr_un *>(storage);
                const auto &path = arg.path;

                if (path.empty() || path.length() - (path.front() == '@' ? 1 : 0) >= sizeof(sockaddr_un::sun_path))
                    return tl::unexpected(make_error_code(std::errc::invalid_argument));

                ptr->sun_family = AF_UNIX;
                memcpy(ptr->sun_path, path.c_str(), path.length());

                result.second = sizeof(sa_family_t) + path.length() + 1;

                if (path.front() == '@') {
                    result.second--;
                    ptr->sun_path[0] = '\0';
                }

                return result;
            }
#endif
            else
                return tl::unexpected(make_error_code(std::errc::address_family_not_supported));
        },
        address
    );
}

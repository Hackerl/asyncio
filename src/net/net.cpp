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
asyncio::net::IPv4Address::from(const std::string &ip, unsigned short port) {
    std::array<std::byte, 4> ipv4 = {};

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
asyncio::net::IPv6Address::from(const std::string &ip, unsigned short port) {
    unsigned int index = 0;
    std::array<std::byte, 16> ipv6 = {};

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
asyncio::net::addressFrom(const std::string &ip, unsigned short port) {
    auto result = IPv6Address::from(ip, port);

    if (result)
        return result;

    return IPv4Address::from(ip, port);
}

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::addressFrom(FileDescriptor fd, bool peer) {
    sockaddr_storage storage = {};
    socklen_t length = sizeof(sockaddr_storage);

    if ((peer ? getpeername : getsockname)(fd, (sockaddr *) &storage, &length) < 0)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return addressFrom((const sockaddr *) &storage, length);
}

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::addressFrom(const sockaddr *addr, socklen_t length) {
    tl::expected<Address, std::error_code> result;

    switch (addr->sa_family) {
        case AF_INET: {
            auto address = (const sockaddr_in *) addr;

            IPv4Address ipv4 = {};

            ipv4.port = ntohs(address->sin_port);
            memcpy(ipv4.ip.data(), &address->sin_addr, sizeof(in_addr));

            result = ipv4;
            break;
        }

        case AF_INET6: {
            auto address = (const sockaddr_in6 *) addr;

            IPv6Address ipv6 = {};

            ipv6.port = ntohs(address->sin6_port);
            memcpy(ipv6.ip.data(), &address->sin6_addr, sizeof(in6_addr));

            if (address->sin6_scope_id == 0) {
                result = ipv6;
                break;
            }

            char name[IF_NAMESIZE];

            if (!if_indextoname(address->sin6_scope_id, name))
                break;

            ipv6.zone = name;
            result = ipv6;

            break;
        }

#if __unix__ || __APPLE__
        case AF_UNIX: {
            if (length == sizeof(sa_family_t)) {
                result = UnixAddress{};
                break;
            }

            auto address = (const sockaddr_un *) addr;

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

tl::expected<std::vector<std::byte>, std::error_code> asyncio::net::socketAddressFrom(const Address &address) {
    tl::expected<std::vector<std::byte>, std::error_code> result;
    sockaddr_storage storage = {};

    switch (address.index()) {
        case 0: {
            auto ipv4 = std::get<IPv4Address>(address);
            auto ptr = (sockaddr_in *) &storage;

            ptr->sin_family = AF_INET;
            ptr->sin_port = htons(ipv4.port);
            memcpy(&ptr->sin_addr, ipv4.ip.data(), sizeof(in_addr));

            result = {(const std::byte *) &storage, (const std::byte *) &storage + sizeof(sockaddr_in)};
            break;
        }

        case 1: {
            auto ipv6 = std::get<IPv6Address>(address);
            auto ptr = (sockaddr_in6 *) &storage;

            ptr->sin6_family = AF_INET6;
            ptr->sin6_port = htons(ipv6.port);
            memcpy(&ptr->sin6_addr, ipv6.ip.data(), sizeof(in6_addr));

            if (!ipv6.zone) {
                result = {(const std::byte *) &storage, (const std::byte *) &storage + sizeof(sockaddr_in6)};
                break;
            }

            unsigned int index = if_nametoindex(ipv6.zone->c_str());

            if (!index) {
                result = tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
                break;
            }

            ptr->sin6_scope_id = index;
            result = {(const std::byte *) &storage, (const std::byte *) &storage + sizeof(sockaddr_in6)};
            break;
        }

#if __unix__ || __APPLE__
        case 2: {
            auto ptr = (sockaddr_un *) &storage;

            ptr->sun_family = AF_UNIX;
            strncpy(ptr->sun_path, std::get<UnixAddress>(address).path.c_str(), sizeof(sockaddr_un::sun_path) - 1);

            result = {(const std::byte *) &storage, (const std::byte *) &storage + sizeof(sockaddr_un)};
            break;
        }
#endif

        default:
            result = tl::unexpected(make_error_code(std::errc::address_family_not_supported));
            break;
    }

    return result;
}

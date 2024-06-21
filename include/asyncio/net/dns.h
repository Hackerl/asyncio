#ifndef ASYNCIO_DNS_H
#define ASYNCIO_DNS_H

#include "net.h"

namespace asyncio::net::dns {
    task::Task<std::vector<Address>, std::error_code>
    getAddressInfo(std::string node, std::optional<std::string> service, std::optional<addrinfo> hints);

    task::Task<std::vector<IP>, std::error_code> lookupIP(std::string host);

    task::Task<std::vector<IPv4>, std::error_code> lookupIPv4(std::string host);
    task::Task<std::vector<IPv6>, std::error_code> lookupIPv6(std::string host);
}

#endif //ASYNCIO_DNS_H

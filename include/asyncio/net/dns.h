#ifndef ASYNCIO_DNS_H
#define ASYNCIO_DNS_H

#include "net.h"

namespace asyncio::net::dns {
    zero::async::coroutine::Task<std::vector<Address>, std::error_code>
    getAddressInfo(std::string node, std::optional<std::string> service, std::optional<addrinfo> hints);

    zero::async::coroutine::Task<std::vector<IP>, std::error_code> lookupIP(std::string host);

    zero::async::coroutine::Task<std::vector<IPv4>, std::error_code> lookupIPv4(std::string host);
    zero::async::coroutine::Task<std::vector<IPv6>, std::error_code> lookupIPv6(std::string host);
}

#endif //ASYNCIO_DNS_H

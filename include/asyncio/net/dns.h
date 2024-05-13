#ifndef ASYNCIO_DNS_H
#define ASYNCIO_DNS_H

#include "net.h"

namespace asyncio::net::dns {
    using AddressInfo = evutil_addrinfo;

    DEFINE_ERROR_TRANSFORMER_EX(
        Error,
        "asyncio::net::dns",
        evutil_gai_strerror,
        EVUTIL_EAI_CANCEL, std::errc::operation_canceled,
        EVUTIL_EAI_ADDRFAMILY, std::errc::address_family_not_supported,
        EVUTIL_EAI_AGAIN, std::errc::resource_unavailable_try_again,
        EVUTIL_EAI_BADFLAGS, std::errc::invalid_argument,
        EVUTIL_EAI_MEMORY, std::errc::not_enough_memory,
        EVUTIL_EAI_FAMILY, std::errc::not_supported,
        EVUTIL_EAI_SERVICE, std::errc::not_supported,
        EVUTIL_EAI_SOCKTYPE, std::errc::not_supported
    )

    zero::async::coroutine::Task<std::vector<Address>, std::error_code>
    getAddressInfo(std::string node, std::optional<std::string> service, std::optional<AddressInfo> hints);

    zero::async::coroutine::Task<std::vector<IP>, std::error_code> lookupIP(std::string host);

    zero::async::coroutine::Task<std::vector<IPv4>, std::error_code> lookupIPv4(std::string host);
    zero::async::coroutine::Task<std::vector<IPv6>, std::error_code> lookupIPv6(std::string host);
}

DECLARE_ERROR_CODE(asyncio::net::dns::Error)

#endif //ASYNCIO_DNS_H

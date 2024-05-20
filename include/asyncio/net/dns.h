#ifndef ASYNCIO_DNS_H
#define ASYNCIO_DNS_H

#include "net.h"

namespace asyncio::net::dns {
    using AddressInfo = evutil_addrinfo;

    DEFINE_ERROR_TRANSFORMER_EX(
        Error,
        "asyncio::net::dns",
        evutil_gai_strerror,
        [](const int value) {
            std::optional<std::error_condition> condition;

            switch (value) {
            case EVUTIL_EAI_CANCEL:
                condition = std::errc::operation_canceled;
                break;

            case EVUTIL_EAI_ADDRFAMILY:
                condition = std::errc::address_family_not_supported;
                break;

            case EVUTIL_EAI_AGAIN:
                condition = std::errc::resource_unavailable_try_again;
                break;

            case EVUTIL_EAI_BADFLAGS:
                condition = std::errc::invalid_argument;
                break;

            case EVUTIL_EAI_MEMORY:
                condition = std::errc::not_enough_memory;
                break;

            case EVUTIL_EAI_FAMILY:
            case EVUTIL_EAI_SERVICE:
            case EVUTIL_EAI_SOCKTYPE:
                condition = std::errc::not_supported;
                break;

            default:
                break;
            }

            return condition;
        }
    )

    zero::async::coroutine::Task<std::vector<Address>, std::error_code>
    getAddressInfo(std::string node, std::optional<std::string> service, std::optional<AddressInfo> hints);

    zero::async::coroutine::Task<std::vector<IP>, std::error_code> lookupIP(std::string host);

    zero::async::coroutine::Task<std::vector<IPv4>, std::error_code> lookupIPv4(std::string host);
    zero::async::coroutine::Task<std::vector<IPv6>, std::error_code> lookupIPv6(std::string host);
}

DECLARE_ERROR_CODE(asyncio::net::dns::Error)

#endif //ASYNCIO_DNS_H

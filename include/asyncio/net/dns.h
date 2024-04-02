#ifndef ASYNCIO_DNS_H
#define ASYNCIO_DNS_H

#include "net.h"

namespace asyncio::net::dns {
    using AddressInfo = evutil_addrinfo;

    enum Error {
    };

    class ErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
        [[nodiscard]] std::error_condition default_error_condition(int value) const noexcept override;
    };

    const std::error_category &errorCategory();
    std::error_code make_error_code(Error e);

    zero::async::coroutine::Task<std::vector<Address>, std::error_code>
    getAddressInfo(std::string node, std::optional<std::string> service, std::optional<AddressInfo> hints);

    zero::async::coroutine::Task<std::vector<IP>, std::error_code> lookupIP(std::string host);

    zero::async::coroutine::Task<std::vector<IPv4>, std::error_code> lookupIPv4(std::string host);
    zero::async::coroutine::Task<std::vector<IPv6>, std::error_code> lookupIPv6(std::string host);
}

template<>
struct std::is_error_code_enum<asyncio::net::dns::Error> : std::true_type {
};

#endif //ASYNCIO_DNS_H

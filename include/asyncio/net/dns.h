#ifndef ASYNCIO_DNS_H
#define ASYNCIO_DNS_H

#include "net.h"

namespace asyncio::net::dns {
    enum Error {

    };

    class Category : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
    };

    const std::error_category &category();
    std::error_code make_error_code(Error e);

    zero::async::coroutine::Task<std::vector<Address>, std::error_code>
    getAddressInfo(std::string node, std::optional<std::string> service, std::optional<evutil_addrinfo> hints);

    zero::async::coroutine::Task<std::vector<std::variant<std::array<std::byte, 4>, std::array<std::byte, 16>>>, std::error_code>
    lookupIP(std::string host);

    zero::async::coroutine::Task<std::vector<std::array<std::byte, 4>>, std::error_code>
    lookupIPv4(std::string host);

    zero::async::coroutine::Task<std::vector<std::array<std::byte, 16>>, std::error_code>
    lookupIPv6(std::string host);
}

namespace std {
    template<>
    struct is_error_code_enum<asyncio::net::dns::Error> : public true_type {

    };
}

#endif //ASYNCIO_DNS_H

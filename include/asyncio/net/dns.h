#ifndef ASYNCIO_DNS_H
#define ASYNCIO_DNS_H

#include "net.h"
#include <event2/dns.h>

namespace asyncio::net::dns {
    enum Error {
        FORMAT = DNS_ERR_FORMAT,
        SERVER_FAILED = DNS_ERR_SERVERFAILED,
        NOT_EXIST = DNS_ERR_NOTEXIST,
        NOT_IMPL = DNS_ERR_NOTIMPL,
        REFUSED = DNS_ERR_REFUSED,
        TRUNCATED = DNS_ERR_TRUNCATED,
        UNKNOWN = DNS_ERR_UNKNOWN,
        TIMEOUT = DNS_ERR_TIMEOUT,
        SHUTDOWN = DNS_ERR_SHUTDOWN,
        CANCEL = DNS_ERR_CANCEL,
        NODATA = DNS_ERR_NODATA
    };

    class Category : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
    };

    const std::error_category &category();
    std::error_code make_error_code(Error e);

    zero::async::coroutine::Task<std::vector<Address>, std::error_code>
    getAddressInfo(
            const std::string &node,
            const std::optional<std::string> &service,
            const std::optional<evutil_addrinfo> &hints
    );

    zero::async::coroutine::Task<std::vector<std::variant<std::array<std::byte, 4>, std::array<std::byte, 16>>>, std::error_code>
    lookupIP(const std::string &host);

    zero::async::coroutine::Task<std::vector<std::array<std::byte, 4>>, std::error_code>
    lookupIPv4(const std::string &host);

    zero::async::coroutine::Task<std::vector<std::array<std::byte, 16>>, std::error_code>
    lookupIPv6(const std::string &host);
}

namespace std {
    template<>
    struct is_error_code_enum<asyncio::net::dns::Error> : public true_type {

    };
}

#endif //ASYNCIO_DNS_H

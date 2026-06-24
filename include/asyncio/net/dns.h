#ifndef ASYNCIO_DNS_H
#define ASYNCIO_DNS_H

#include "net.h"

namespace asyncio::net::dns {
#ifdef ASYNCIO_ENABLE_C_ARES
    class Resolver {
        struct Core;

        explicit Resolver(std::unique_ptr<Core> core);

    public:
        static Resolver make();
        static Resolver &current();

        [[nodiscard]] const std::vector<std::string> &getServers() const;
        std::expected<void, std::error_code> setServers(std::vector<std::string> servers);

        void cancelAll();

        task::Task<std::vector<Address>, std::error_code>
        getAddressInfo(std::string node, std::optional<std::string> service, std::optional<addrinfo> hints);

        task::Task<std::vector<IP>, std::error_code> lookupIP(std::string host);

        task::Task<std::vector<IPv4>, std::error_code> lookupIPv4(std::string host);
        task::Task<std::vector<IPv6>, std::error_code> lookupIPv6(std::string host);

    private:
        std::unique_ptr<Core> mCore;
        std::vector<std::string> mServers;
    };

    const std::vector<std::string> &getServers();
    std::expected<void, std::error_code> setServers(std::vector<std::string> servers);
#endif

    task::Task<std::vector<Address>, std::error_code>
    getAddressInfo(std::string node, std::optional<std::string> service, std::optional<addrinfo> hints);

    task::Task<std::vector<IP>, std::error_code> lookupIP(std::string host);

    task::Task<std::vector<IPv4>, std::error_code> lookupIPv4(std::string host);
    task::Task<std::vector<IPv6>, std::error_code> lookupIPv6(std::string host);
}

#endif //ASYNCIO_DNS_H

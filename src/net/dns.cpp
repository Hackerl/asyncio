#include <asyncio/net/dns.h>
#include <asyncio/promise.h>
#include <ranges>

zero::async::coroutine::Task<std::vector<asyncio::net::Address>, std::error_code>
asyncio::net::dns::getAddressInfo(
    const std::string node,
    const std::optional<std::string> service,
    const std::optional<AddressInfo> hints
) {
    const auto dnsBase = getEventLoop()->makeDNSBase();
    CO_EXPECT(dnsBase);

    Promise<std::vector<Address>, Error> promise;

    evdns_getaddrinfo_request *request = evdns_getaddrinfo(
        dnsBase->get(),
        node.c_str(),
        service ? service->c_str() : nullptr,
        hints ? &*hints : nullptr,
        [](int result, evutil_addrinfo *res, void *arg) {
            const auto p = static_cast<Promise<std::vector<Address>, Error> *>(arg);

            if (result != 0) {
                p->reject(static_cast<Error>(result));
                return;
            }

            std::vector<Address> addresses;

            for (auto i = res; i; i = i->ai_next) {
#ifdef _WIN32
                auto address = addressFrom(i->ai_addr, static_cast<socklen_t>(i->ai_addrlen));
#else
                auto address = addressFrom(i->ai_addr, i->ai_addrlen);
#endif

                if (!address)
                    continue;

                addresses.push_back(*std::move(address));
            }

            evutil_freeaddrinfo(res);
            p->resolve(std::move(addresses));
        },
        &promise
    );

    if (!request)
        co_return co_await promise.getFuture();

    co_return co_await zero::async::coroutine::Cancellable{
        promise.getFuture(),
        [=]() -> tl::expected<void, std::error_code> {
            evdns_getaddrinfo_cancel(request);
            return {};
        }
    };
}

zero::async::coroutine::Task<std::vector<asyncio::net::IP>, std::error_code>
asyncio::net::dns::lookupIP(std::string host) {
    AddressInfo hints = {};

    hints.ai_flags = EVUTIL_AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    co_return (co_await getAddressInfo(std::move(host), std::nullopt, hints))
        .transform([](std::span<const Address> addresses) {
            const auto v = addresses
                | std::views::transform(
                    [](const Address &address) -> IP {
                        if (std::holds_alternative<IPv4Address>(address))
                            return std::get<IPv4Address>(address).ip;

                        return std::get<IPv6Address>(address).ip;
                    }
                );

            return std::vector<IP>{v.begin(), v.end()};
        });
}

zero::async::coroutine::Task<std::vector<asyncio::net::IPv4>, std::error_code>
asyncio::net::dns::lookupIPv4(std::string host) {
    AddressInfo hints = {};

    hints.ai_flags = EVUTIL_AI_ADDRCONFIG;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    co_return co_await getAddressInfo(std::move(host), std::nullopt, hints)
        .transform([](std::span<const Address> addresses) {
            const auto v = addresses
                | std::views::transform(
                    [](const Address &address) {
                        return std::get<IPv4Address>(address).ip;
                    }
                );

            return std::vector<IPv4>{v.begin(), v.end()};
        });
}

zero::async::coroutine::Task<std::vector<asyncio::net::IPv6>, std::error_code>
asyncio::net::dns::lookupIPv6(std::string host) {
    AddressInfo hints = {};

    hints.ai_flags = EVUTIL_AI_ADDRCONFIG;
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;

    co_return co_await getAddressInfo(std::move(host), std::nullopt, hints)
        .transform([](std::span<const Address> addresses) {
            const auto v = addresses
                | std::views::transform(
                    [](const Address &address) {
                        return std::get<IPv6Address>(address).ip;
                    }
                );

            return std::vector<IPv6>{v.begin(), v.end()};
        });
}

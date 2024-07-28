#include <asyncio/net/dns.h>

asyncio::task::Task<std::vector<asyncio::net::Address>, std::error_code>
asyncio::net::dns::getAddressInfo(
    const std::string node,
    const std::optional<std::string> service,
    const std::optional<addrinfo> hints
) {
    Promise<std::vector<Address>, uv::Error> promise;
    uv_getaddrinfo_t request = {.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_getaddrinfo(
            getEventLoop()->raw(),
            &request,
            // ReSharper disable once CppParameterMayBeConstPtrOrRef
            [](uv_getaddrinfo_t *req, const int status, addrinfo *res) {
                const auto p = static_cast<Promise<std::vector<Address>, uv::Error> *>(req->data);

                if (status < 0) {
                    p->reject(static_cast<uv::Error>(status));
                    return;
                }

                std::vector<Address> addresses;

                for (auto i = res; i; i = i->ai_next) {
                    auto address = addressFrom(i->ai_addr, static_cast<socklen_t>(i->ai_addrlen));

                    if (!address)
                        continue;

                    addresses.push_back(*std::move(address));
                }

                uv_freeaddrinfo(res);
                p->resolve(std::move(addresses));
            },
            node.c_str(),
            service ? service->c_str() : nullptr,
            hints ? &*hints : nullptr
        );
    }));

    co_return co_await task::Cancellable{
        promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            EXPECT(uv::expected([&] {
                return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
            }));
            return {};
        }
    };
}

asyncio::task::Task<std::vector<asyncio::net::IP>, std::error_code>
asyncio::net::dns::lookupIP(std::string host) {
    addrinfo hints = {};

    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC;

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

asyncio::task::Task<std::vector<asyncio::net::IPv4>, std::error_code>
asyncio::net::dns::lookupIPv4(std::string host) {
    addrinfo hints = {};

    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_family = AF_INET;

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

asyncio::task::Task<std::vector<asyncio::net::IPv6>, std::error_code>
asyncio::net::dns::lookupIPv6(std::string host) {
    addrinfo hints = {};

    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_family = AF_INET6;

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

#include <asyncio/net/dns.h>
#include <zero/defer.h>

asyncio::task::Task<std::vector<asyncio::net::Address>, std::error_code>
asyncio::net::dns::getAddressInfo(
    const std::string node,
    const std::optional<std::string> service,
    const std::optional<addrinfo> hints
) {
    Promise<std::vector<Address>, uv::Error> promise;
    uv_getaddrinfo_t request{.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_getaddrinfo(
            getEventLoop()->raw(),
            &request,
            [](auto *req, const int status, addrinfo *result) {
                const auto p = static_cast<Promise<std::vector<Address>, uv::Error> *>(req->data);

                if (status < 0) {
                    p->reject(static_cast<uv::Error>(status));
                    return;
                }

                DEFER(uv_freeaddrinfo(result));
                std::vector<Address> addresses;

                for (const auto *ptr = result; ptr; ptr = ptr->ai_next) {
                    auto address = addressFrom(ptr->ai_addr, static_cast<socklen_t>(ptr->ai_addrlen));

                    if (!address)
                        continue;

                    addresses.push_back(*std::move(address));
                }

                std::ranges::sort(addresses);
                const auto [first, last] = std::ranges::unique(addresses);
                addresses.erase(first, last);

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
    co_return co_await getAddressInfo(
        std::move(host),
        std::nullopt,
        addrinfo{
            .ai_flags = AI_ADDRCONFIG,
            .ai_family = AF_UNSPEC
        }
    ).transform([](const auto &addresses) {
        return addresses
            | std::views::transform(
                [](const auto &address) -> IP {
                    if (std::holds_alternative<IPv4Address>(address))
                        return std::get<IPv4Address>(address).ip;

                    return std::get<IPv6Address>(address).ip;
                }
            )
            | std::ranges::to<std::vector>();
    });
}

asyncio::task::Task<std::vector<asyncio::net::IPv4>, std::error_code>
asyncio::net::dns::lookupIPv4(std::string host) {
    co_return co_await getAddressInfo(
        std::move(host),
        std::nullopt,
        addrinfo{
            .ai_flags = AI_ADDRCONFIG,
            .ai_family = AF_INET
        }
    ).transform([](const auto &addresses) {
        return addresses
            | std::views::transform(
                [](const auto &address) {
                    return std::get<IPv4Address>(address).ip;
                }
            )
            | std::ranges::to<std::vector>();
    });
}

asyncio::task::Task<std::vector<asyncio::net::IPv6>, std::error_code>
asyncio::net::dns::lookupIPv6(std::string host) {
    co_return co_await getAddressInfo(
        std::move(host),
        std::nullopt,
        addrinfo{
            .ai_flags = AI_ADDRCONFIG,
            .ai_family = AF_INET6
        }
    ).transform([](std::span<const Address> addresses) {
        return addresses
            | std::views::transform(
                [](const auto &address) {
                    return std::get<IPv6Address>(address).ip;
                }
            )
            | std::ranges::to<std::vector>();
    });
}

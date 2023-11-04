#include <asyncio/net/dns.h>
#include <asyncio/event_loop.h>
#include <ranges>

const char *asyncio::net::dns::Category::name() const noexcept {
    return "asyncio::net::dns";
}

std::string asyncio::net::dns::Category::message(int value) const {
    return evutil_gai_strerror(value);
}

const std::error_category &asyncio::net::dns::category() {
    static Category instance;
    return instance;
}

std::error_code asyncio::net::dns::make_error_code(Error e) {
    return {static_cast<int>(e), category()};
}

zero::async::coroutine::Task<std::vector<asyncio::net::Address>, std::error_code>
asyncio::net::dns::getAddressInfo(std::string node, std::optional<std::string> service, std::optional<addrinfo> hints) {
    zero::async::promise::Promise<std::vector<Address>, Error> promise;

    evdns_getaddrinfo_request *request = evdns_getaddrinfo(
            getEventLoop()->dnsBase(),
            node.c_str(),
            service ? service->c_str() : nullptr,
            hints ? &*hints : nullptr,
            [](int result, evutil_addrinfo *res, void *arg) {
                auto promise = static_cast<zero::async::promise::Promise<std::vector<Address>, Error> *>(arg);

                if (result != 0) {
                    promise->reject((Error) result);
                    delete promise;
                    return;
                }

                std::vector<Address> addresses;

                for (auto i = res; i; i = i->ai_next) {
                    auto address = addressFrom(i->ai_addr, i->ai_addrlen);

                    if (!address)
                        continue;

                    addresses.push_back(std::move(*address));
                }

                evutil_freeaddrinfo(res);
                promise->resolve(std::move(addresses));
                delete promise;
            },
            new zero::async::promise::Promise<std::vector<Address>, Error>(promise)
    );

    if (!request)
        co_return co_await promise;

    co_return co_await zero::async::coroutine::Cancellable{
            promise,
            [=]() -> tl::expected<void, std::error_code> {
                evdns_getaddrinfo_cancel(request);
                return {};
            }
    };
}

zero::async::coroutine::Task<std::vector<std::variant<std::array<std::byte, 4>, std::array<std::byte, 16>>>, std::error_code>
asyncio::net::dns::lookupIP(std::string host) {
    using IPAddress = std::variant<std::array<std::byte, 4>, std::array<std::byte, 16>>;

    evutil_addrinfo hints = {};

    hints.ai_flags = EVUTIL_AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    co_return (co_await getAddressInfo(std::move(host), std::nullopt, hints))
            .transform([](std::span<const Address> addresses) {
                auto v = addresses
                         | std::views::transform(
                        [](const Address &address) -> IPAddress {
                            if (address.index() == 0)
                                return std::get<IPv4Address>(address).ip;

                            return std::get<IPv6Address>(address).ip;
                        }
                );

                return std::vector<IPAddress>{v.begin(), v.end()};
            });
}

zero::async::coroutine::Task<std::vector<std::array<std::byte, 4>>, std::error_code>
asyncio::net::dns::lookupIPv4(std::string host) {
    evutil_addrinfo hints = {};

    hints.ai_flags = EVUTIL_AI_ADDRCONFIG;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    co_return (co_await getAddressInfo(std::move(host), std::nullopt, hints))
            .transform([](std::span<const Address> addresses) {
                auto v = addresses
                         | std::views::transform(
                        [](const Address &address) {
                            return std::get<IPv4Address>(address).ip;
                        }
                );

                return std::vector<std::array<std::byte, 4>>{v.begin(), v.end()};
            });
}

zero::async::coroutine::Task<std::vector<std::array<std::byte, 16>>, std::error_code>
asyncio::net::dns::lookupIPv6(std::string host) {
    evutil_addrinfo hints = {};

    hints.ai_flags = EVUTIL_AI_ADDRCONFIG;
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;

    co_return (co_await getAddressInfo(std::move(host), std::nullopt, hints))
            .transform([](std::span<const Address> addresses) {
                auto v = addresses
                         | std::views::transform(
                        [](const Address &address) {
                            return std::get<IPv6Address>(address).ip;
                        }
                );

                return std::vector<std::array<std::byte, 16>>{v.begin(), v.end()};
            });
}

#include <asyncio/net/dns.h>
#include <asyncio/event_loop.h>
#include <ranges>

const char *asyncio::net::dns::ErrorCategory::name() const noexcept {
    return "asyncio::net::dns";
}

std::string asyncio::net::dns::ErrorCategory::message(const int value) const {
    return evutil_gai_strerror(value);
}

const std::error_category &asyncio::net::dns::errorCategory() {
    static ErrorCategory instance;
    return instance;
}

std::error_code asyncio::net::dns::make_error_code(const Error e) {
    return {static_cast<int>(e), errorCategory()};
}

zero::async::coroutine::Task<std::vector<asyncio::net::Address>, std::error_code>
asyncio::net::dns::getAddressInfo(
    const std::string node,
    const std::optional<std::string> service,
    const std::optional<AddressInfo> hints
) {
    zero::async::promise::Promise<std::vector<Address>, Error> promise;

    evdns_getaddrinfo_request *request = evdns_getaddrinfo(
        getEventLoop()->dnsBase(),
        node.c_str(),
        service ? service->c_str() : nullptr,
        hints ? &*hints : nullptr,
        [](int result, evutil_addrinfo *res, void *arg) {
            const auto p = static_cast<zero::async::promise::Promise<std::vector<Address>, Error> *>(arg);

            if (result != 0) {
                p->reject(static_cast<Error>(result));
                delete p;
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

                addresses.push_back(std::move(*address));
            }

            evutil_freeaddrinfo(res);
            p->resolve(std::move(addresses));
            delete p;
        },
        new zero::async::promise::Promise(promise)
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

zero::async::coroutine::Task<
    std::vector<std::variant<std::array<std::byte, 4>, std::array<std::byte, 16>>>,
    std::error_code
>
asyncio::net::dns::lookupIP(std::string host) {
    using IPAddress = std::variant<std::array<std::byte, 4>, std::array<std::byte, 16>>;

    AddressInfo hints = {};

    hints.ai_flags = EVUTIL_AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    co_return (co_await getAddressInfo(std::move(host), std::nullopt, hints))
        .transform([](std::span<const Address> addresses) {
            const auto v = addresses
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

            return std::vector<std::array<std::byte, 4>>{v.begin(), v.end()};
        });
}

zero::async::coroutine::Task<std::vector<std::array<std::byte, 16>>, std::error_code>
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

            return std::vector<std::array<std::byte, 16>>{v.begin(), v.end()};
        });
}

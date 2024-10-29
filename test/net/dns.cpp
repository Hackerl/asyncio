#include <catch_extensions.h>
#include <asyncio/net/dns.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

ASYNC_TEST_CASE("get address info", "[net]") {
    const auto result = co_await asyncio::net::dns::getAddressInfo(
        "localhost",
        "http",
        addrinfo{
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM
        }
    );
    REQUIRE(result);

    REQUIRE_THAT(
        *result,
        Catch::Matchers::AllMatch(Catch::Matchers::Predicate<asyncio::net::Address>([&](const auto &address) {
            return std::visit(
                []<typename T>(const T &arg) -> bool {
                    if constexpr (std::is_same_v<T, asyncio::net::IPv4Address>)
                        return arg.ip == asyncio::net::LOCALHOST_IPV4 && arg.port == 80;
                    else if constexpr (std::is_same_v<T, asyncio::net::IPv6Address>)
                        return arg.ip == asyncio::net::LOCALHOST_IPV6 && arg.port == 80;
                    else
                        std::abort();
                },
                address
            );
        }))
    );
}

ASYNC_TEST_CASE("lookup IP", "[net]") {
    const auto result = co_await asyncio::net::dns::lookupIP("localhost");
    REQUIRE(result);

    REQUIRE_THAT(
        *result,
        Catch::Matchers::AllMatch(Catch::Matchers::Predicate<asyncio::net::IP>([&](const auto &ip) {
            return std::visit(
                []<typename T>(const T &arg) -> bool {
                    if constexpr (std::is_same_v<T, asyncio::net::IPv4>)
                        return arg == asyncio::net::LOCALHOST_IPV4;
                    else if constexpr (std::is_same_v<T, asyncio::net::IPv6>)
                        return arg == asyncio::net::LOCALHOST_IPV6;
                    else
                        std::abort();
                },
                ip
            );
        }))
    );
}

ASYNC_TEST_CASE("lookup IPv4", "[net]") {
    const auto result = co_await asyncio::net::dns::lookupIPv4("localhost");
    REQUIRE(result);
    REQUIRE_THAT(*result, Catch::Matchers::SizeIs(1));
    REQUIRE(result->front() == asyncio::net::LOCALHOST_IPV4);
}

ASYNC_TEST_CASE("lookup IPv6", "[net]") {
    if (const auto result = co_await asyncio::net::dns::lookupIPv6("localhost"); result && !result->empty()) {
        REQUIRE_THAT(*result, Catch::Matchers::SizeIs(1));
        REQUIRE(result->front() == asyncio::net::LOCALHOST_IPV6);
    }
}

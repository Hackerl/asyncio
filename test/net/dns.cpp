#include <catch_extensions.h>
#include <asyncio/net/dns.h>
#include <catch2/matchers/catch_matchers_all.hpp>

ASYNC_TEST_CASE("get address info", "[net::dns][integration]") {
    const auto result = co_await asyncio::net::dns::getAddressInfo(
        "one.one.one.one",
        "domain",
        addrinfo{
            .ai_flags = AI_ADDRCONFIG,
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM
        }
    );
    REQUIRE(result);

    REQUIRE_THAT(
        *result
        | std::views::transform([](const auto &address) {
            return std::visit(
                [](const auto &arg) {
                    return fmt::to_string(arg);
                },
                address
            );
        }),
        Catch::Matchers::Contains("1.1.1.1:53") &&
        Catch::Matchers::Contains("1.0.0.1:53")
    );
}

ASYNC_TEST_CASE("lookup IP", "[net::dns][integration]") {
    const auto result = co_await asyncio::net::dns::lookupIP("one.one.one.one");
    REQUIRE(result);

    REQUIRE_THAT(
        *result
        | std::views::transform([](const auto &address) {
            return std::visit(
                [](const auto &arg) {
                    return zero::os::net::stringify(arg);
                },
                address
            );
        }),
        Catch::Matchers::Contains("1.1.1.1") &&
        Catch::Matchers::Contains("1.0.0.1")
    );
}

ASYNC_TEST_CASE("lookup IPv4", "[net::dns][integration]") {
    const auto result = co_await asyncio::net::dns::lookupIPv4("one.one.one.one");
    REQUIRE(result);

    REQUIRE_THAT(
        *result
        | std::views::transform([](const auto &address) {
            return zero::os::net::stringify(address);
        }),
        Catch::Matchers::Contains("1.1.1.1") &&
        Catch::Matchers::Contains("1.0.0.1")
    );
}

ASYNC_TEST_CASE("lookup IPv6", "[net::dns][integration]") {
    if (const auto result = co_await asyncio::net::dns::lookupIPv6("one.one.one.one"); result && !result->empty()) {
        REQUIRE_THAT(
            *result
            | std::views::transform([](const auto &address) {
                return zero::os::net::stringify(address);
            }),
            Catch::Matchers::Contains("2606:4700:4700::1111") &&
            Catch::Matchers::Contains("2606:4700:4700::1001")
        );
    }
}

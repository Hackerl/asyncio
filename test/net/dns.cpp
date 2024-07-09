#include <asyncio/net/dns.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("DNS query", "[net]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        SECTION("get address info") {
            addrinfo hints = {};

            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;

            const auto res = co_await asyncio::net::dns::getAddressInfo("localhost", "http", hints);
            REQUIRE(res);

            REQUIRE(
                std::ranges::all_of(
                    *res,
                    [](const auto &address) {
                        if (std::holds_alternative<asyncio::net::IPv4Address>(address))
                            return fmt::to_string(std::get<asyncio::net::IPv4Address>(address)) == "127.0.0.1:80";

                        return fmt::to_string(std::get<asyncio::net::IPv6Address>(address)) == "[::1]:80";
                    }
                )
            );
        }

        SECTION("lookup IP") {
            const auto res = co_await asyncio::net::dns::lookupIP("localhost");
            REQUIRE(res);

            REQUIRE(
                std::ranges::all_of(
                    *res,
                    [](const auto &ip) {
                        if (std::holds_alternative<asyncio::net::IPv4>(ip))
                            return zero::os::net::stringify(std::get<asyncio::net::IPv4>(ip)) == "127.0.0.1";

                        return zero::os::net::stringify(std::get<asyncio::net::IPv6>(ip)) == "::1";
                    }
                )
            );
        }

        SECTION("lookup IPv4") {
            const auto res = co_await asyncio::net::dns::lookupIPv4("localhost");
            REQUIRE(res);
            REQUIRE(!res->empty());
            REQUIRE(
                std::ranges::any_of(
                    *res,
                    [](const auto &ip) {
                        return zero::os::net::stringify(ip) == "127.0.0.1";
                    }
                )
            );
        }

        SECTION("lookup IPv6") {
            if (const auto res = co_await asyncio::net::dns::lookupIPv6("localhost");
                res && !res->empty()) {
                REQUIRE(
                    std::ranges::any_of(
                        *res,
                        [](const auto &ip) {
                            return zero::os::net::stringify(ip) == "::1";
                        }
                    )
                );
            }
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

#include <asyncio/net/dns.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("DNS query", "[net]") {
    const auto result = asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("get address info") {
            addrinfo hints = {};

            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;

            const auto res = co_await asyncio::net::dns::getAddressInfo("localhost", "http", hints);
            REQUIRE(res);

            REQUIRE(
                std::all_of(
                    res->begin(),
                    res->end(),
                    [](const auto &address) {
                        if (std::holds_alternative<asyncio::net::IPv4Address>(address))
                            return std::get<asyncio::net::IPv4Address>(address).port == 80;

                        return std::get<asyncio::net::IPv6Address>(address).port == 80;
                    }
                )
            );
        }

        SECTION("lookup IP") {
            const auto res = co_await asyncio::net::dns::lookupIP("localhost");
            REQUIRE(res);

            REQUIRE(
                std::all_of(
                    res->begin(),
                    res->end(),
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
            REQUIRE(res->size() == 1);
            REQUIRE(zero::os::net::stringify(res->front()) == "127.0.0.1");
        }

        SECTION("lookup IPv6") {
            const auto res = co_await asyncio::net::dns::lookupIPv6("localhost");
            REQUIRE((!res || res->empty() || zero::os::net::stringify(res->front()) == "::1"));
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

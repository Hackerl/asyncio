#include <asyncio/net/dns.h>
#include <asyncio/event_loop.h>
#include <zero/os/net.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("DNS query", "[net]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("get address info") {
            asyncio::net::dns::AddressInfo hints = {};

            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;

            const auto result = co_await asyncio::net::dns::getAddressInfo("localhost", "http", hints);
            REQUIRE(result);

            REQUIRE(
                std::all_of(
                    result->begin(),
                    result->end(),
                    [](const auto &address) {
                        if (address.index() == 0)
                            return std::get<asyncio::net::IPv4Address>(address).port == 80;

                        return std::get<asyncio::net::IPv6Address>(address).port == 80;
                    }
                )
            );
        }

        SECTION("lookup IP") {
            const auto result = co_await asyncio::net::dns::lookupIP("localhost");
            REQUIRE(result);

            REQUIRE(
                std::all_of(
                    result->begin(),
                    result->end(),
                    [](const auto &ip) {
                        if (ip.index() == 0)
                            return zero::os::net::stringify(std::get<0>(ip)) == "127.0.0.1";

                        return zero::os::net::stringify(std::get<1>(ip)) == "::1";
                    }
                )
            );
        }

        SECTION("lookup IPv4") {
            const auto result = co_await asyncio::net::dns::lookupIPv4("localhost");
            REQUIRE(result);
            REQUIRE(result->size() == 1);
            REQUIRE(zero::os::net::stringify(result->front()) == "127.0.0.1");
        }

        SECTION("lookup IPv6") {
            const auto result = co_await asyncio::net::dns::lookupIPv6("localhost");
            REQUIRE((!result || result->empty() || zero::os::net::stringify(result->front()) == "::1"));
        }
    });
}

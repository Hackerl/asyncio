#include <asyncio/net/dns.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("DNS query", "[dns]") {
    SECTION("get address info") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            evutil_addrinfo hints = {};

            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;

            auto result = co_await asyncio::net::dns::getAddressInfo("localhost", "http", hints);
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
        });
    }

    SECTION("lookup IP") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto result = co_await asyncio::net::dns::lookupIP("localhost");
            REQUIRE(result);

            REQUIRE(
                    std::all_of(
                            result->begin(),
                            result->end(),
                            [](const auto &ip) {
                                if (ip.index() == 0)
                                    return memcmp(std::get<0>(ip).data(), "\x7f\x00\x00\x01", 4) == 0;

                                return memcmp(
                                        std::get<1>(ip).data(),
                                        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
                                        16
                                ) == 0;
                            }
                    )
            );
        });
    }

    SECTION("lookup IPv4") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto result = co_await asyncio::net::dns::lookupIPv4("localhost");

            REQUIRE(result);
            REQUIRE(result->size() == 1);
            REQUIRE(memcmp(result->front().data(), "\x7f\x00\x00\x01", 4) == 0);
        });
    }

    SECTION("lookup IPv6") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto result = co_await asyncio::net::dns::lookupIPv6("localhost");

            REQUIRE(result);
            REQUIRE(result->size() == 1);
            REQUIRE(memcmp(
                    result->front().data(),
                    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
                    16) == 0
            );
        });
    }
}
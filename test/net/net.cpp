#include <asyncio/net/net.h>
#include <catch2/catch_test_macros.hpp>
#include <regex>

#ifdef _WIN32
#include <netioapi.h>
#elif defined(__linux__)
#include <net/if.h>
#include <netinet/in.h>
#elif defined(__APPLE__)
#include <net/if.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <sys/un.h>
#endif

TEST_CASE("network components", "[net]") {
    SECTION("IPv4") {
        const asyncio::net::Address address = asyncio::net::IPv4Address{
            80,
            {std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}}
        };

        REQUIRE(address == asyncio::net::IPv4Address{80, {std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}}});
        REQUIRE(address != asyncio::net::IPv4Address{80, {std::byte{127}, std::byte{0}, std::byte{0}, std::byte{0}}});
        REQUIRE(address != asyncio::net::IPv6Address{});
        REQUIRE(address != asyncio::net::UnixAddress{});
        REQUIRE(address != asyncio::net::UnixAddress{});

        REQUIRE(fmt::to_string(address) == "variant(127.0.0.1:80)");

        REQUIRE(*asyncio::net::addressFrom("127.0.0.1", 80) == address);
        REQUIRE(*asyncio::net::IPv4Address::from("127.0.0.1", 80) == address);

        const auto socketAddress = socketAddressFrom(address);
        REQUIRE(socketAddress);
        REQUIRE(socketAddress->second == sizeof(sockaddr_in));

        const auto addr = reinterpret_cast<const sockaddr_in *>(socketAddress->first.get());

        REQUIRE(addr->sin_family == AF_INET);
        REQUIRE(addr->sin_port == htons(80));
        REQUIRE(memcmp(&addr->sin_addr, "\x7f\x00\x00\x01", 4) == 0);
    }

    SECTION("mapped IPv6") {
        constexpr auto ipv4Address = asyncio::net::IPv4Address{
            80,
            {std::byte{8}, std::byte{8}, std::byte{8}, std::byte{8}}
        };

        const auto ipv6Address = asyncio::net::IPv6Address::from(ipv4Address);

        REQUIRE(ipv6Address.port == 80);
        REQUIRE(zero::os::net::stringify(ipv6Address.ip) == "::ffff:8.8.8.8");
        REQUIRE(fmt::to_string(ipv6Address) == "[::ffff:8.8.8.8]:80");
    }

    SECTION("IPv6") {
        const auto interfaces = zero::os::net::interfaces();
        REQUIRE(interfaces);
        REQUIRE_FALSE(interfaces->empty());

        const auto &zone = std::views::keys(*interfaces).front();
        const auto index = if_nametoindex(zone.c_str());
        REQUIRE(index);

        const asyncio::net::Address address = asyncio::net::IPv6Address{80, {}, zone};

        REQUIRE(address == asyncio::net::IPv6Address{80, {}, zone});
        REQUIRE(address != asyncio::net::IPv6Address{80, {}});
        REQUIRE(address != asyncio::net::IPv6Address{80, {std::byte{127}}, zone});
        REQUIRE(address != asyncio::net::IPv4Address{});
        REQUIRE(address != asyncio::net::UnixAddress{});

        REQUIRE(std::regex_match(fmt::to_string(address), std::regex(R"(variant\(\[::%.*\]:80\))")));

        REQUIRE(*asyncio::net::addressFrom("::", 80) != address);
        REQUIRE(*asyncio::net::IPv6Address::from("::", 80) != address);
        REQUIRE(*asyncio::net::addressFrom(fmt::format("::%{}", zone), 80) == address);
        REQUIRE(*asyncio::net::IPv6Address::from(fmt::format("::%{}", zone), 80) == address);

        const auto socketAddress = socketAddressFrom(address);
        REQUIRE(socketAddress);
        REQUIRE(socketAddress->second == sizeof(sockaddr_in6));

        const auto addr = reinterpret_cast<const sockaddr_in6 *>(socketAddress->first.get());

        REQUIRE(addr->sin6_family == AF_INET6);
        REQUIRE(addr->sin6_port == htons(80));
        REQUIRE(addr->sin6_scope_id == index);
        REQUIRE(
            std::all_of(
                (const std::byte *) &addr->sin6_addr,
                (const std::byte *) &addr->sin6_addr + sizeof(sockaddr_in6::sin6_addr),
                [](const auto &byte) {
                    return byte == std::byte{0};
                }
            )
        );
    }

#if defined(__unix__) || defined(__APPLE__)
    SECTION("UNIX") {
        using namespace std::string_view_literals;
        const asyncio::net::Address address = asyncio::net::UnixAddress{"/tmp/test.sock"};

        REQUIRE(address == asyncio::net::UnixAddress{"/tmp/test.sock"});
        REQUIRE(address != asyncio::net::UnixAddress{"/root/test.sock"});
        REQUIRE(address != asyncio::net::IPv4Address{});
        REQUIRE(address != asyncio::net::IPv6Address{});

        REQUIRE(fmt::to_string(address) == "variant(/tmp/test.sock)");

        const auto socketAddress = socketAddressFrom(address);
        REQUIRE(socketAddress);
        REQUIRE(socketAddress->second == sizeof(sa_family_t) + "/tmp/test.sock"sv.size() + 1);

        const auto addr = reinterpret_cast<const sockaddr_un *>(socketAddress->first.get());

        REQUIRE(addr->sun_family == AF_UNIX);
        REQUIRE(addr->sun_path == "/tmp/test.sock"sv);
    }
#endif
}

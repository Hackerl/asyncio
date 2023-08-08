#include <asyncio/net/net.h>
#include <zero/os/net.h>
#include <zero/strings/strings.h>
#include <catch2/catch_test_macros.hpp>
#include <regex>

#ifdef _WIN32
#include <netioapi.h>
#elif __linux__
#include <net/if.h>
#include <netinet/in.h>
#endif

#ifdef __unix__
#include <sys/un.h>
#endif

TEST_CASE("network components", "[network]") {
    SECTION("IPv4") {
        asyncio::net::Address address = asyncio::net::IPv4Address{
                80,
                {std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}}
        };

        REQUIRE(address == asyncio::net::IPv4Address{80, {std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}}});
        REQUIRE(address != asyncio::net::IPv4Address{80, {std::byte{127}, std::byte{0}, std::byte{0}, std::byte{0}}});
        REQUIRE(address != asyncio::net::IPv6Address{});
        REQUIRE(address != asyncio::net::UnixAddress{});
        REQUIRE(address != asyncio::net::UnixAddress{});

        REQUIRE(asyncio::net::stringify(address) == "127.0.0.1:80");

        REQUIRE(*asyncio::net::addressFrom("127.0.0.1", 80) == address);
        REQUIRE(*asyncio::net::IPv4Address::from("127.0.0.1", 80) == address);

        auto socketAddress = asyncio::net::socketAddressFrom(address);
        REQUIRE(socketAddress);

        auto addr = (const sockaddr_in *) &*socketAddress;

        REQUIRE(addr->sin_family == AF_INET);
        REQUIRE(addr->sin_port == htons(80));
        REQUIRE(memcmp(&addr->sin_addr, "\x7f\x00\x00\x01", 4) == 0);
    }

    SECTION("mapped IPv6") {
        auto ipv4Address = asyncio::net::IPv4Address{80, {std::byte{8}, std::byte{8}, std::byte{8}, std::byte{8}}};
        auto ipv6Address = asyncio::net::IPv6Address::from(ipv4Address);

        REQUIRE(ipv6Address.port == 80);
        REQUIRE(zero::os::net::stringify(ipv6Address.ip) == "::ffff:8.8.8.8");
        REQUIRE(asyncio::net::stringify(ipv6Address) == "[::ffff:8.8.8.8]:80");
    }

    SECTION("IPv6") {
        char name[IF_NAMESIZE];
        REQUIRE(if_indextoname(1, name));

        asyncio::net::Address address = asyncio::net::IPv6Address{80, {}, name};

        REQUIRE(address == asyncio::net::IPv6Address{80, {}, name});
        REQUIRE(address != asyncio::net::IPv6Address{80, {}});
        REQUIRE(address != asyncio::net::IPv6Address{80, {std::byte{127}}, name});
        REQUIRE(address != asyncio::net::IPv4Address{});
        REQUIRE(address != asyncio::net::UnixAddress{});

        REQUIRE(std::regex_match(asyncio::net::stringify(address), std::regex(R"(\[::%.*\]:80)")));

        REQUIRE(*asyncio::net::addressFrom("::", 80) != address);
        REQUIRE(*asyncio::net::IPv6Address::from("::", 80) != address);
        REQUIRE(*asyncio::net::addressFrom(zero::strings::format("::%%%s", name), 80) == address);
        REQUIRE(*asyncio::net::IPv6Address::from(zero::strings::format("::%%%s", name), 80) == address);

        auto socketAddress = asyncio::net::socketAddressFrom(address);
        REQUIRE(socketAddress);

        auto addr = (const sockaddr_in6 *) &*socketAddress;

        REQUIRE(addr->sin6_family == AF_INET6);
        REQUIRE(addr->sin6_port == htons(80));
        REQUIRE(addr->sin6_scope_id == 1);
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

#ifdef __unix__
    SECTION("UNIX") {
        asyncio::net::Address address = asyncio::net::UnixAddress{"/tmp/test.sock"};

        REQUIRE(address == asyncio::net::UnixAddress{"/tmp/test.sock"});
        REQUIRE(address != asyncio::net::UnixAddress{"/root/test.sock"});
        REQUIRE(address != asyncio::net::IPv4Address{});
        REQUIRE(address != asyncio::net::IPv6Address{});

        REQUIRE(asyncio::net::stringify(address) == "/tmp/test.sock");

        auto socketAddress = asyncio::net::socketAddressFrom(address);
        REQUIRE(socketAddress);

        auto addr = (const sockaddr_un *) &*socketAddress;

        REQUIRE(addr->sun_family == AF_UNIX);
        REQUIRE(strcmp(addr->sun_path, "/tmp/test.sock") == 0);
    }
#endif
}
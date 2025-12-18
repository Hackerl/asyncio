#include <catch_extensions.h>
#include <asyncio/net/net.h>
#include <asyncio/stream.h>
#include <catch2/matchers/catch_matchers_all.hpp>

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

TEST_CASE("IPv4 address", "[net]") {
    SECTION("parse") {
        SECTION("valid") {
            REQUIRE(
                asyncio::net::IPv4Address::from("127.0.0.1", 80) ==
                asyncio::net::IPv4Address{asyncio::net::LOCALHOST_IPV4, 80}
            );
        }

        SECTION("invalid") {
            REQUIRE_ERROR(asyncio::net::IPv4Address::from("127.0.0", 80), std::errc::invalid_argument);
        }
    }

    SECTION("stringify") {
        REQUIRE(fmt::to_string(asyncio::net::IPv4Address{asyncio::net::LOCALHOST_IPV4, 80}) == "127.0.0.1:80");
    }
}

TEST_CASE("IPv6 address", "[net]") {
    SECTION("parse") {
        SECTION("valid") {
            SECTION("with zone") {
                REQUIRE(
                    asyncio::net::IPv6Address::from("::1%eth0", 80) ==
                    asyncio::net::IPv6Address{asyncio::net::LOCALHOST_IPV6, 80, "eth0"}
                );
            }

            SECTION("without zone") {
                REQUIRE(
                    asyncio::net::IPv6Address::from("::1", 80) ==
                    asyncio::net::IPv6Address{asyncio::net::LOCALHOST_IPV6, 80}
                );
            }
        }

        SECTION("invalid") {
            REQUIRE_ERROR(asyncio::net::IPv6Address::from(":", 80), std::errc::invalid_argument);
        }
    }

    SECTION("mapped") {
        REQUIRE(
            asyncio::net::IPv6Address::from(asyncio::net::IPv4Address{asyncio::net::LOCALHOST_IPV4, 80}) ==
            asyncio::net::IPv6Address{
                {
                    std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                    std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                    std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
                    std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}
                },
                80
            }
        );
    }

    SECTION("stringify") {
        SECTION("with zone") {
            REQUIRE(
                fmt::to_string(asyncio::net::IPv6Address{asyncio::net::LOCALHOST_IPV6, 80, "eth0"}) == "[::1%eth0]:80"
            );
        }

        SECTION("without zone") {
            REQUIRE(fmt::to_string(asyncio::net::IPv6Address{asyncio::net::LOCALHOST_IPV6, 80}) == "[::1]:80");
        }
    }
}

TEST_CASE("unix address", "[net]") {
    SECTION("filesystem") {
        REQUIRE(fmt::to_string(asyncio::net::UnixAddress{"/tmp/test.sock"}) == "unix:///tmp/test.sock");
    }

    SECTION("abstract") {
        REQUIRE(fmt::to_string(asyncio::net::UnixAddress{"@test.sock"}) == "unix://@test.sock");
    }
}

TEST_CASE("convert network address to socket address", "[net]") {
    SECTION("IPv4") {
        const auto address = socketAddressFrom(asyncio::net::IPv4Address{asyncio::net::LOCALHOST_IPV4, 80});
        REQUIRE(address);

        const auto ptr = reinterpret_cast<const sockaddr_in *>(address->first.get());
        REQUIRE(ptr->sin_family == AF_INET);
        REQUIRE(ptr->sin_port == htons(80));
        REQUIRE(std::memcmp(&ptr->sin_addr, "\x7f\x00\x00\x01", 4) == 0);
    }

    SECTION("IPv6") {
        const auto interfaces = zero::os::net::interfaces();
        REQUIRE(interfaces);
        REQUIRE_THAT(*interfaces, !Catch::Matchers::IsEmpty());

        const auto &zone = std::views::keys(*interfaces).front();
        const auto index = if_nametoindex(zone.c_str());
        REQUIRE(index != 0);

        const auto address = socketAddressFrom(asyncio::net::IPv6Address{asyncio::net::LOCALHOST_IPV6, 80, zone});
        REQUIRE(address);

        const auto ptr = reinterpret_cast<const sockaddr_in6 *>(address->first.get());
        REQUIRE(ptr->sin6_family == AF_INET6);
        REQUIRE(ptr->sin6_port == htons(80));
        REQUIRE(ptr->sin6_scope_id == index);
        REQUIRE(
            std::memcmp(&ptr->sin6_addr, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16) == 0
        );
    }

#if defined(__unix__) || defined(__APPLE__)
    SECTION("unix") {
        SECTION("filesystem") {
            // ReSharper disable once CppVariableCanBeMadeConstexpr
            const std::string path{"/tmp/test.sock"};

            const auto address = socketAddressFrom(asyncio::net::UnixAddress{path});
            REQUIRE(address);
            REQUIRE(address->second == sizeof(sa_family_t) + path.size() + 1);

            const auto ptr = reinterpret_cast<const sockaddr_un *>(address->first.get());
            REQUIRE(ptr->sun_family == AF_UNIX);
            REQUIRE(ptr->sun_path == path);
        }

#ifdef __linux__
        SECTION("abstract") {
            // ReSharper disable once CppVariableCanBeMadeConstexpr
            const std::string path{"@test.sock"};

            const auto address = socketAddressFrom(asyncio::net::UnixAddress{path});
            REQUIRE(address);
            REQUIRE(address->second == sizeof(sa_family_t) + path.size());

            const auto ptr = reinterpret_cast<const sockaddr_un *>(address->first.get());
            REQUIRE(ptr->sun_family == AF_UNIX);
            REQUIRE(ptr->sun_path[0] == '\0');
            REQUIRE(ptr->sun_path + 1 == path.substr(1));
        }
#endif
    }
#endif
}

TEST_CASE("convert socket address to network address", "[net]") {
    SECTION("IPv4") {
        sockaddr_in addr{};

        addr.sin_family = AF_INET;
        addr.sin_port = htons(80);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        REQUIRE(
            asyncio::net::addressFrom(reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) ==
            asyncio::net::IPv4Address{asyncio::net::LOCALHOST_IPV4, 80}
        );
    }

    SECTION("IPv6") {
        const sockaddr_in6 addr{
            .sin6_family = AF_INET6,
            .sin6_port = htons(80),
            .sin6_addr = IN6ADDR_LOOPBACK_INIT
        };

        REQUIRE(
            asyncio::net::addressFrom(reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) ==
            asyncio::net::IPv6Address{asyncio::net::LOCALHOST_IPV6, 80}
        );
    }

#if defined(__unix__) || defined(__APPLE__)
    SECTION("unix") {
        SECTION("filesystem") {
            // ReSharper disable once CppVariableCanBeMadeConstexpr
            const std::string path{"/tmp/test.sock"};

            sockaddr_un addr{};

            addr.sun_family = AF_UNIX;
            std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path));

            REQUIRE(
                asyncio::net::addressFrom(
                    reinterpret_cast<const sockaddr *>(&addr),
                    sizeof(sa_family_t) + path.size() + 1
                ) == asyncio::net::UnixAddress{path}
            );
        }

#ifdef __linux__
        SECTION("abstract") {
            // ReSharper disable once CppVariableCanBeMadeConstexpr
            const std::string path{"@test.sock"};

            sockaddr_un addr{};

            addr.sun_family = AF_UNIX;
            addr.sun_path[0] = '\0';
            std::strncpy(addr.sun_path + 1, path.c_str() + 1, sizeof(addr.sun_path) - 1);

            REQUIRE(
                asyncio::net::addressFrom(
                    reinterpret_cast<const sockaddr *>(&addr),
                    sizeof(sa_family_t) + path.size()
                ) == asyncio::net::UnixAddress{path}
            );
        }
#endif
    }
#endif
}

ASYNC_TEST_CASE("copy bidirectional", "[net]") {
    const auto input = GENERATE(take(10, randomBytes(1, 102400)));

    auto pair1 = asyncio::Stream::pair();
    auto pair2 = asyncio::Stream::pair();

    auto task = asyncio::net::copyBidirectional(pair1.at(1), pair2.at(0));

    auto &stream1 = pair1.at(0);
    auto &stream2 = pair2.at(1);

    auto task1 = stream2.readAll();

    REQUIRE(co_await stream1.writeAll(input));
    REQUIRE(co_await stream1.shutdown());
    REQUIRE(co_await task1 == input);

    auto task2 = stream1.readAll();

    REQUIRE(co_await stream2.writeAll(input));
    REQUIRE(co_await stream2.shutdown());
    REQUIRE(co_await task2 == input);

    const auto result = co_await task;
    REQUIRE(result);
    REQUIRE(result->at(0) == input.size());
    REQUIRE(result->at(1) == input.size());
}

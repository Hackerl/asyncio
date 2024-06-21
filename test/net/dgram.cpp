#include <asyncio/net/dgram.h>
#include <catch2/catch_test_macros.hpp>

constexpr std::string_view MESSAGE = "hello world";

TEST_CASE("datagram network connection", "[net]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        auto server = asyncio::net::UDPSocket::bind("127.0.0.1", 30000);
        REQUIRE(server);

        SECTION("normal") {
            co_await allSettled(
                [](auto socket) -> asyncio::task::Task<void> {
                    std::string message;
                    message.resize(MESSAGE.size());

                    const auto res = co_await socket.readFrom(std::as_writable_bytes(std::span{message}));
                    REQUIRE(res);

                    const auto &[num, from] = *res;
                    REQUIRE(num);
                    REQUIRE(num == MESSAGE.size());
                    REQUIRE(std::holds_alternative<asyncio::net::IPv4Address>(from));
                    REQUIRE(fmt::to_string(from) == "variant(127.0.0.1:30001)");
                    REQUIRE(message == MESSAGE);

                    const auto n = co_await socket.writeTo(std::as_bytes(std::span{message}), from);
                    REQUIRE(n);
                    REQUIRE(*n == num);
                }(*std::move(server)),
                []() -> asyncio::task::Task<void> {
                    using namespace std::string_view_literals;

                    auto socket = asyncio::net::UDPSocket::bind("127.0.0.1", 30001);
                    REQUIRE(socket);

                    const auto address = asyncio::net::IPv4Address::from("127.0.0.1", 30000);
                    REQUIRE(address);

                    const auto n = co_await socket->writeTo(std::as_bytes(std::span{"hello world"sv}), *address);
                    REQUIRE(n);
                    REQUIRE(*n == MESSAGE.size());

                    std::string message;
                    message.resize(MESSAGE.size());

                    const auto res = co_await socket->readFrom(std::as_writable_bytes(std::span{message}));
                    REQUIRE(res);

                    const auto &[num, from] = *res;
                    REQUIRE(num);
                    REQUIRE(num == MESSAGE.size());
                    REQUIRE(std::holds_alternative<asyncio::net::IPv4Address>(from));
                    REQUIRE(fmt::to_string(from) == "variant(127.0.0.1:30000)");
                    REQUIRE(message == MESSAGE);
                }()
            );
        }

        SECTION("connect") {
            co_await allSettled(
                [](auto socket) -> asyncio::task::Task<void> {
                    std::string message;
                    message.resize(MESSAGE.size());

                    const auto res = co_await socket.readFrom(std::as_writable_bytes(std::span{message}));
                    REQUIRE(res);

                    const auto &[num, from] = *res;
                    REQUIRE(num);
                    REQUIRE(num == MESSAGE.size());
                    REQUIRE(std::holds_alternative<asyncio::net::IPv4Address>(from));
                    REQUIRE(fmt::to_string(from).find("127.0.0.1") != std::string::npos);
                    REQUIRE(message == MESSAGE);

                    const auto n = co_await socket.writeTo(std::as_bytes(std::span{message}), from);
                    REQUIRE(n);
                    REQUIRE(*n == num);
                }(*std::move(server)),
                []() -> asyncio::task::Task<void> {
                    auto socket = co_await asyncio::net::UDPSocket::connect("127.0.0.1", 30000);
                    REQUIRE(socket);

                    const auto n = co_await socket->write(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(n);
                    REQUIRE(*n == MESSAGE.size());

                    std::string message;
                    message.resize(MESSAGE.size());

                    const auto res = co_await socket->readFrom(std::as_writable_bytes(std::span{message}));
                    REQUIRE(res);

                    const auto &[num, from] = *res;
                    REQUIRE(num);
                    REQUIRE(num == MESSAGE.size());
                    REQUIRE(std::holds_alternative<asyncio::net::IPv4Address>(from));
                    REQUIRE(fmt::to_string(from) == "variant(127.0.0.1:30000)");
                    REQUIRE(message == MESSAGE);
                }()
            );
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

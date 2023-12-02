#include <asyncio/net/dgram.h>
#include <asyncio/event_loop.h>
#include <asyncio/error.h>
#include <catch2/catch_test_macros.hpp>
#include <fmt/std.h>

using namespace std::chrono_literals;

constexpr std::string_view MESSAGE = "hello";

TEST_CASE("datagram network connection", "[dgram]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        auto server = asyncio::net::dgram::bind("127.0.0.1", 30000);
        REQUIRE(server);

        SECTION("normal") {
            co_await allSettled(
                [](auto s) -> zero::async::coroutine::Task<void> {
                    std::byte data[1024];
                    const auto result = co_await s.readFrom(data);
                    REQUIRE(result);

                    const auto &[num, from] = *result;
                    REQUIRE(num);
                    REQUIRE(num == MESSAGE.size());
                    REQUIRE(from.index() == 0);
                    REQUIRE(fmt::to_string(from) == "variant(127.0.0.1:30001)");
                    REQUIRE(memcmp(data, MESSAGE.data(), MESSAGE.size()) == 0);

                    const auto n = co_await s.writeTo({data, num}, from);
                    REQUIRE(n);
                    REQUIRE(*n == num);
                }(std::move(*server)),
                []() -> zero::async::coroutine::Task<void> {
                    auto client = asyncio::net::dgram::bind("127.0.0.1", 30001);
                    REQUIRE(client);

                    const auto address = asyncio::net::IPv4Address::from("127.0.0.1", 30000);
                    REQUIRE(address);

                    const auto n = co_await client->writeTo(std::as_bytes(std::span{MESSAGE}), *address);
                    REQUIRE(n);
                    REQUIRE(*n == MESSAGE.size());

                    std::byte data[1024];
                    const auto res = co_await client->readFrom(data);
                    REQUIRE(res);

                    const auto &[num, from] = *res;
                    REQUIRE(num);
                    REQUIRE(num == MESSAGE.size());
                    REQUIRE(from.index() == 0);
                    REQUIRE(fmt::to_string(from) == "variant(127.0.0.1:30000)");
                    REQUIRE(memcmp(data, MESSAGE.data(), MESSAGE.size()) == 0);
                }()
            );
        }

        SECTION("connect") {
            co_await allSettled(
                [](auto s) -> zero::async::coroutine::Task<void> {
                    std::byte data[1024];
                    const auto result = co_await s.readFrom(data);
                    REQUIRE(result);

                    const auto &[num, from] = *result;
                    REQUIRE(num);
                    REQUIRE(num == MESSAGE.size());
                    REQUIRE(from.index() == 0);
                    REQUIRE(fmt::to_string(from).find("127.0.0.1") != std::string::npos);
                    REQUIRE(memcmp(data, MESSAGE.data(), MESSAGE.size()) == 0);

                    const auto n = co_await s.writeTo({data, num}, from);
                    REQUIRE(n);
                    REQUIRE(*n == num);
                }(std::move(*server)),
                []() -> zero::async::coroutine::Task<void> {
                    auto client = std::move(co_await asyncio::net::dgram::connect("127.0.0.1", 30000));
                    REQUIRE(client);

                    const auto n = co_await client->write(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(n);
                    REQUIRE(*n == MESSAGE.size());

                    std::byte data[1024];
                    const auto result = co_await client->readFrom(data);
                    REQUIRE(result);

                    const auto &[num, from] = *result;
                    REQUIRE(num);
                    REQUIRE(num == MESSAGE.size());
                    REQUIRE(from.index() == 0);
                    REQUIRE(fmt::to_string(from) == "variant(127.0.0.1:30000)");
                    REQUIRE(memcmp(data, MESSAGE.data(), MESSAGE.size()) == 0);
                }()
            );
        }

        SECTION("read timeout") {
            server->setTimeout(50ms, 0ms);

            std::byte data[1024];
            const auto result = co_await server->readFrom(data);
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        }

        SECTION("cancel") {
            std::byte data[1024];
            const auto task = server->readFrom(data);
            const auto result = co_await asyncio::timeout(task, 50ms);

            REQUIRE(task.done());
            REQUIRE(task.result().error() == std::errc::operation_canceled);
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        }
    });
}

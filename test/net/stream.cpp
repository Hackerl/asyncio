#include <asyncio/net/stream.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <fmt/std.h>

constexpr std::string_view MESSAGE = "hello world";

TEST_CASE("stream network connection", "[net]") {
    const auto result = asyncio::run([]() -> zero::async::coroutine::Task<void> {
        auto listener = asyncio::net::TCPListener::listen("127.0.0.1", 30000);
        REQUIRE(listener);

        co_await allSettled(
            [](auto l) -> zero::async::coroutine::Task<void> {
                using namespace std::string_view_literals;

                auto stream = co_await l.accept();
                REQUIRE(stream);

                const auto localAddress = stream->localAddress();
                REQUIRE(localAddress);
                REQUIRE(fmt::to_string(*localAddress) == "variant(127.0.0.1:30000)");

                const auto remoteAddress = stream->remoteAddress();
                REQUIRE(remoteAddress);
                REQUIRE(fmt::to_string(*remoteAddress).find("127.0.0.1") != std::string::npos);

                auto res = co_await stream->writeAll(std::as_bytes(std::span{MESSAGE}));
                REQUIRE(res);

                std::string message;
                message.resize(MESSAGE.size());

                res = co_await stream->readExactly(std::as_writable_bytes(std::span{message}));
                REQUIRE(res);
                REQUIRE(message == MESSAGE);
            }(*std::move(listener)),
            []() -> zero::async::coroutine::Task<void> {
                auto stream = co_await asyncio::net::TCPStream::connect("127.0.0.1", 30000);
                REQUIRE(stream);

                const auto localAddress = stream->localAddress();
                REQUIRE(localAddress);
                REQUIRE(fmt::to_string(*localAddress).find("127.0.0.1") != std::string::npos);

                const auto remoteAddress = stream->remoteAddress();
                REQUIRE(remoteAddress);
                REQUIRE(fmt::to_string(*remoteAddress) == "variant(127.0.0.1:30000)");

                std::string message;
                message.resize(MESSAGE.size());

                auto res = co_await stream->readExactly(std::as_writable_bytes(std::span{message}));
                REQUIRE(res);
                REQUIRE(message == MESSAGE);

                res = co_await stream->writeAll(std::as_bytes(std::span{message}));
                REQUIRE(res);
            }()
        );
    });
    REQUIRE(result);
    REQUIRE(*result);
}

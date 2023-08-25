#include <asyncio/net/stream.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("stream network connection", "[stream]") {
    SECTION("TCP") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto listener = asyncio::net::stream::listen("127.0.0.1", 30000);
            REQUIRE(listener);

            co_await zero::async::coroutine::allSettled(
                    [](auto listener) -> zero::async::coroutine::Task<void> {
                        auto buffer = std::move(co_await listener.accept());
                        REQUIRE(buffer);

                        auto localAddress = buffer->localAddress();
                        REQUIRE(localAddress);
                        REQUIRE(asyncio::net::stringify(*localAddress) == "127.0.0.1:30000");

                        auto remoteAddress = buffer->remoteAddress();
                        REQUIRE(remoteAddress);
                        REQUIRE(asyncio::net::stringify(*remoteAddress).starts_with("127.0.0.1"));

                        buffer->writeLine("hello world");
                        co_await buffer->drain();

                        auto line = co_await buffer->readLine();

                        REQUIRE(line);
                        REQUIRE(*line == "world hello");

                        buffer->close();
                        listener.close();
                    }(std::move(*listener)),
                    []() -> zero::async::coroutine::Task<void> {
                        auto buffer = std::move(co_await asyncio::net::stream::connect("127.0.0.1", 30000));
                        REQUIRE(buffer);

                        auto localAddress = buffer->localAddress();
                        REQUIRE(localAddress);
                        REQUIRE(asyncio::net::stringify(*localAddress).starts_with("127.0.0.1"));

                        auto remoteAddress = buffer->remoteAddress();
                        REQUIRE(remoteAddress);
                        REQUIRE(asyncio::net::stringify(*remoteAddress) == "127.0.0.1:30000");

                        auto line = co_await buffer->readLine();

                        REQUIRE(line);
                        REQUIRE(*line == "hello world");

                        buffer->writeLine("world hello");
                        co_await buffer->drain();
                        co_await buffer->waitClosed();
                    }()
            );
        });
    }

#if __unix__ || __APPLE__
    SECTION("UNIX domain") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto listener = asyncio::net::stream::listen("/tmp/asyncio-test.sock");
            REQUIRE(listener);

            co_await zero::async::coroutine::allSettled(
                    [](auto listener) -> zero::async::coroutine::Task<void> {
                        auto buffer = std::move(co_await listener.accept());
                        REQUIRE(buffer);

                        auto localAddress = buffer->localAddress();
                        REQUIRE(localAddress);
                        REQUIRE(asyncio::net::stringify(*localAddress) == "/tmp/asyncio-test.sock");

                        buffer->writeLine("hello world");
                        co_await buffer->drain();

                        auto line = co_await buffer->readLine();

                        REQUIRE(line);
                        REQUIRE(*line == "world hello");

                        buffer->close();
                        listener.close();

                        remove("/tmp/asyncio-test.sock");
                    }(std::move(*listener)),
                    []() -> zero::async::coroutine::Task<void> {
                        auto buffer = std::move(co_await asyncio::net::stream::connect("/tmp/asyncio-test.sock"));
                        REQUIRE(buffer);

                        auto remoteAddress = buffer->remoteAddress();
                        REQUIRE(remoteAddress);
                        REQUIRE(asyncio::net::stringify(*remoteAddress) == "/tmp/asyncio-test.sock");

                        auto line = co_await buffer->readLine();

                        REQUIRE(line);
                        REQUIRE(*line == "hello world");

                        buffer->writeLine("world hello");
                        co_await buffer->drain();
                        co_await buffer->waitClosed();
                    }()
            );
        });
    }
#endif
}
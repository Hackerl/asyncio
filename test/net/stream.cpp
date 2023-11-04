#include <asyncio/net/stream.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

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
        SECTION("filesystem") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto path = std::filesystem::temp_directory_path() / "asyncio-test.sock";
                auto listener = asyncio::net::stream::listen(path.string());
                REQUIRE(listener);

                co_await zero::async::coroutine::allSettled(
                        [](auto listener) -> zero::async::coroutine::Task<void> {
                            auto buffer = std::move(co_await listener.accept());
                            REQUIRE(buffer);

                            auto localAddress = buffer->localAddress();
                            REQUIRE(localAddress);
                            REQUIRE(asyncio::net::stringify(*localAddress).ends_with("asyncio-test.sock"));

                            buffer->writeLine("hello world");
                            co_await buffer->drain();

                            auto line = co_await buffer->readLine();

                            REQUIRE(line);
                            REQUIRE(*line == "world hello");

                            buffer->close();
                            listener.close();
                        }(std::move(*listener)),
                        [](auto path) -> zero::async::coroutine::Task<void> {
                            auto buffer = std::move(co_await asyncio::net::stream::connect(path.string()));
                            REQUIRE(buffer);

                            auto remoteAddress = buffer->remoteAddress();
                            REQUIRE(remoteAddress);
                            REQUIRE(asyncio::net::stringify(*remoteAddress).ends_with("asyncio-test.sock"));

                            auto line = co_await buffer->readLine();

                            REQUIRE(line);
                            REQUIRE(*line == "hello world");

                            buffer->writeLine("world hello");
                            co_await buffer->drain();
                            co_await buffer->waitClosed();
                        }(path)
                );

                std::filesystem::remove(path);
            });
        }

#ifdef __linux__
        SECTION("abstract") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto listener = asyncio::net::stream::listen("@asyncio-test.sock");
                REQUIRE(listener);

                co_await zero::async::coroutine::allSettled(
                        [](auto listener) -> zero::async::coroutine::Task<void> {
                            auto buffer = std::move(co_await listener.accept());
                            REQUIRE(buffer);

                            auto localAddress = buffer->localAddress();
                            REQUIRE(localAddress);
                            REQUIRE(asyncio::net::stringify(*localAddress) == "@asyncio-test.sock");

                            buffer->writeLine("hello world");
                            co_await buffer->drain();

                            auto line = co_await buffer->readLine();

                            REQUIRE(line);
                            REQUIRE(*line == "world hello");

                            buffer->close();
                            listener.close();
                        }(std::move(*listener)),
                        []() -> zero::async::coroutine::Task<void> {
                            auto buffer = std::move(co_await asyncio::net::stream::connect("@asyncio-test.sock"));
                            REQUIRE(buffer);

                            auto remoteAddress = buffer->remoteAddress();
                            REQUIRE(remoteAddress);
                            REQUIRE(asyncio::net::stringify(*remoteAddress) == "@asyncio-test.sock");

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
#endif
}
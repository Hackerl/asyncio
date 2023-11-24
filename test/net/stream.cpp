#include <asyncio/net/stream.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <fmt/std.h>

TEST_CASE("stream network connection", "[stream]") {
    SECTION("TCP") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto listener = asyncio::net::stream::listen("127.0.0.1", 30000);
            REQUIRE(listener);

            co_await allSettled(
                [](auto l) -> zero::async::coroutine::Task<void> {
                    auto buffer = std::move(co_await l.accept());
                    REQUIRE(buffer);

                    const auto localAddress = buffer->localAddress();
                    REQUIRE(localAddress);
                    REQUIRE(fmt::to_string(*localAddress) == "variant(127.0.0.1:30000)");

                    const auto remoteAddress = buffer->remoteAddress();
                    REQUIRE(remoteAddress);
                    REQUIRE(fmt::to_string(*remoteAddress).find("127.0.0.1") != std::string::npos);

                    constexpr std::string_view message = "hello world\r\n";
                    auto result = co_await buffer->writeAll(std::as_bytes(std::span{message}));
                    REQUIRE(result);

                    result = co_await buffer->flush();
                    REQUIRE(result);

                    const auto line = co_await buffer->readLine();
                    REQUIRE(line);
                    REQUIRE(*line == "world hello");
                }(std::move(*listener)),
                []() -> zero::async::coroutine::Task<void> {
                    auto buffer = std::move(co_await asyncio::net::stream::connect("127.0.0.1", 30000));
                    REQUIRE(buffer);

                    const auto localAddress = buffer->localAddress();
                    REQUIRE(localAddress);
                    REQUIRE(fmt::to_string(*localAddress).find("127.0.0.1") != std::string::npos);

                    const auto remoteAddress = buffer->remoteAddress();
                    REQUIRE(remoteAddress);
                    REQUIRE(fmt::to_string(*remoteAddress) == "variant(127.0.0.1:30000)");

                    const auto line = co_await buffer->readLine();
                    REQUIRE(line);
                    REQUIRE(*line == "hello world");

                    constexpr std::string_view message = "world hello\r\n";
                    auto result = co_await buffer->writeAll(std::as_bytes(std::span{message}));
                    REQUIRE(result);

                    result = co_await buffer->flush();
                    REQUIRE(result);
                }()
            );
        });
    }

#if __unix__ || __APPLE__
    SECTION("UNIX domain") {
        SECTION("filesystem") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                const auto path = std::filesystem::temp_directory_path() / "asyncio-test.sock";
                auto listener = asyncio::net::stream::listen(path.string());
                REQUIRE(listener);

                co_await allSettled(
                    [](auto l) -> zero::async::coroutine::Task<void> {
                        auto buffer = std::move(co_await l.accept());
                        REQUIRE(buffer);

                        const auto localAddress = buffer->localAddress();
                        REQUIRE(localAddress);
                        REQUIRE(fmt::to_string(*localAddress).find("asyncio-test.sock") != std::string::npos);

                        constexpr std::string_view message = "hello world\r\n";
                        auto result = co_await buffer->writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);

                        const auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "world hello");
                    }(std::move(*listener)),
                    [](auto path) -> zero::async::coroutine::Task<void> {
                        auto buffer = std::move(co_await asyncio::net::stream::connect(path.string()));
                        REQUIRE(buffer);

                        const auto remoteAddress = buffer->remoteAddress();
                        REQUIRE(remoteAddress);
                        REQUIRE(fmt::to_string(*remoteAddress).find("asyncio-test.sock") != std::string::npos);

                        const auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "hello world");

                        constexpr std::string_view message = "world hello\r\n";
                        auto result = co_await buffer->writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);
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

                co_await allSettled(
                    [](auto l) -> zero::async::coroutine::Task<void> {
                        auto buffer = std::move(co_await l.accept());
                        REQUIRE(buffer);

                        const auto localAddress = buffer->localAddress();
                        REQUIRE(localAddress);
                        REQUIRE(fmt::to_string(*localAddress) == "variant(@asyncio-test.sock)");

                        constexpr std::string_view message = "hello world\r\n";
                        auto result = co_await buffer->writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);

                        const auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "world hello");
                    }(std::move(*listener)),
                    []() -> zero::async::coroutine::Task<void> {
                        auto buffer = std::move(co_await asyncio::net::stream::connect("@asyncio-test.sock"));
                        REQUIRE(buffer);

                        const auto remoteAddress = buffer->remoteAddress();
                        REQUIRE(remoteAddress);
                        REQUIRE(fmt::to_string(*remoteAddress) == "variant(@asyncio-test.sock)");

                        const auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "hello world");

                        constexpr std::string_view message = "world hello\r\n";
                        auto result = co_await buffer->writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);
                    }()
                );
            });
        }
#endif
    }
#endif
}

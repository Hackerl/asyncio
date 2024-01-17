#include <asyncio/net/stream.h>
#include <asyncio/event_loop.h>
#include <zero/strings/strings.h>
#include <catch2/catch_test_macros.hpp>
#include <fmt/std.h>

constexpr std::string_view MESSAGE = "hello world\r\n";

TEST_CASE("stream network connection", "[net]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("TCP") {
            auto listener = asyncio::net::stream::listen("127.0.0.1", 30000);
            REQUIRE(listener);

            co_await allSettled(
                [](auto l) -> zero::async::coroutine::Task<void> {
                    auto buffer = co_await l.accept();
                    REQUIRE(buffer);

                    const auto localAddress = buffer->localAddress();
                    REQUIRE(localAddress);
                    REQUIRE(fmt::to_string(*localAddress) == "variant(127.0.0.1:30000)");

                    const auto remoteAddress = buffer->remoteAddress();
                    REQUIRE(remoteAddress);
                    REQUIRE(fmt::to_string(*remoteAddress).find("127.0.0.1") != std::string::npos);

                    auto result = co_await buffer->writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(result);

                    result = co_await buffer->flush();
                    REQUIRE(result);

                    const auto line = co_await buffer->readLine();
                    REQUIRE(line);
                    REQUIRE(*line == zero::strings::trim(MESSAGE));
                }(std::move(*listener)),
                []() -> zero::async::coroutine::Task<void> {
                    auto buffer = co_await asyncio::net::stream::connect("127.0.0.1", 30000);
                    REQUIRE(buffer);

                    const auto localAddress = buffer->localAddress();
                    REQUIRE(localAddress);
                    REQUIRE(fmt::to_string(*localAddress).find("127.0.0.1") != std::string::npos);

                    const auto remoteAddress = buffer->remoteAddress();
                    REQUIRE(remoteAddress);
                    REQUIRE(fmt::to_string(*remoteAddress) == "variant(127.0.0.1:30000)");

                    const auto line = co_await buffer->readLine();
                    REQUIRE(line);
                    REQUIRE(*line == zero::strings::trim(MESSAGE));

                    auto result = co_await buffer->writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(result);

                    result = co_await buffer->flush();
                    REQUIRE(result);
                }()
            );
        }

#if __unix__ || __APPLE__
        SECTION("UNIX domain") {
            SECTION("filesystem") {
                const auto path = std::filesystem::temp_directory_path() / "asyncio-test.sock";
                auto listener = asyncio::net::stream::listen(path.string());
                REQUIRE(listener);

                co_await allSettled(
                    [](auto l) -> zero::async::coroutine::Task<void> {
                        auto buffer = co_await l.accept();
                        REQUIRE(buffer);

                        const auto localAddress = buffer->localAddress();
                        REQUIRE(localAddress);
                        REQUIRE(fmt::to_string(*localAddress).find("asyncio-test.sock") != std::string::npos);

                        auto result = co_await buffer->writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);

                        const auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == zero::strings::trim(MESSAGE));
                    }(std::move(*listener)),
                    [](auto path) -> zero::async::coroutine::Task<void> {
                        auto buffer = co_await asyncio::net::stream::connect(path.string());
                        REQUIRE(buffer);

                        const auto remoteAddress = buffer->remoteAddress();
                        REQUIRE(remoteAddress);
                        REQUIRE(fmt::to_string(*remoteAddress).find("asyncio-test.sock") != std::string::npos);

                        const auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == zero::strings::trim(MESSAGE));

                        auto result = co_await buffer->writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);
                    }(path)
                );

                REQUIRE(std::filesystem::remove(path));
            }

#ifdef __linux__
            SECTION("abstract") {
                auto listener = asyncio::net::stream::listen("@asyncio-test.sock");
                REQUIRE(listener);

                co_await allSettled(
                    [](auto l) -> zero::async::coroutine::Task<void> {
                        auto buffer = co_await l.accept();
                        REQUIRE(buffer);

                        const auto localAddress = buffer->localAddress();
                        REQUIRE(localAddress);
                        REQUIRE(fmt::to_string(*localAddress) == "variant(@asyncio-test.sock)");

                        auto result = co_await buffer->writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);

                        const auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == zero::strings::trim(MESSAGE));
                    }(std::move(*listener)),
                    []() -> zero::async::coroutine::Task<void> {
                        auto buffer = co_await asyncio::net::stream::connect("@asyncio-test.sock");
                        REQUIRE(buffer);

                        const auto remoteAddress = buffer->remoteAddress();
                        REQUIRE(remoteAddress);
                        REQUIRE(fmt::to_string(*remoteAddress) == "variant(@asyncio-test.sock)");

                        const auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == zero::strings::trim(MESSAGE));

                        auto result = co_await buffer->writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);
                    }()
                );
            }
#endif
        }
#endif
    });
}

#include <asyncio/net/stream.h>
#include <catch2/catch_test_macros.hpp>

constexpr std::string_view MESSAGE = "hello world";

TEST_CASE("stream network connection", "[net]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        SECTION("TCP") {
            auto listener = asyncio::net::TCPListener::listen("127.0.0.1", 30000);
            REQUIRE(listener);

            co_await allSettled(
                [](auto l) -> asyncio::task::Task<void> {
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
                []() -> asyncio::task::Task<void> {
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
        }

#ifdef _WIN32
        SECTION("named pipe") {
            auto listener = asyncio::net::NamedPipeListener::listen(R"(\\.\pipe\asyncio-test)");
            REQUIRE(listener);

            co_await allSettled(
                [](auto l) -> asyncio::task::Task<void> {
                    auto stream = co_await l.accept();
                    REQUIRE(stream);

                    auto res = co_await stream->writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(res);

                    std::string message;
                    message.resize(MESSAGE.size());

                    res = co_await stream->readExactly(std::as_writable_bytes(std::span{message}));
                    REQUIRE(res);
                    REQUIRE(message == MESSAGE);
                }(*std::move(listener)),
                []() -> asyncio::task::Task<void> {
                    auto stream = co_await asyncio::net::NamedPipeStream::connect(R"(\\.\pipe\asyncio-test)");
                    REQUIRE(stream);

                    std::string message;
                    message.resize(MESSAGE.size());

                    auto res = co_await stream->readExactly(std::as_writable_bytes(std::span{message}));
                    REQUIRE(res);
                    REQUIRE(message == MESSAGE);

                    res = co_await stream->writeAll(std::as_bytes(std::span{message}));
                    REQUIRE(res);
                }()
            );
        }
#else
        SECTION("UNIX domain") {
            SECTION("filesystem") {
                const auto path = std::filesystem::temp_directory_path() / "asyncio-test.sock";
                auto listener = asyncio::net::UnixListener::listen(path.string());
                REQUIRE(listener);

                co_await allSettled(
                    [](auto l) -> asyncio::task::Task<void> {
                        auto stream = co_await l.accept();
                        REQUIRE(stream);

                        const auto localAddress = stream->localAddress();
                        REQUIRE(localAddress);
                        REQUIRE(fmt::to_string(*localAddress).find("asyncio-test.sock") != std::string::npos);

                        auto res = co_await stream->writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(res);

                        std::string message;
                        message.resize(MESSAGE.size());

                        res = co_await stream->readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == MESSAGE);
                    }(*std::move(listener)),
                    [](auto p) -> asyncio::task::Task<void> {
                        auto stream = co_await asyncio::net::UnixStream::connect(p);
                        REQUIRE(stream);

                        const auto remoteAddress = stream->remoteAddress();
                        REQUIRE(remoteAddress);
                        REQUIRE(fmt::to_string(*remoteAddress).find("asyncio-test.sock") != std::string::npos);

                        std::string message;
                        message.resize(MESSAGE.size());

                        auto res = co_await stream->readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == MESSAGE);

                        res = co_await stream->writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(res);
                    }(path)
                );

                REQUIRE(std::filesystem::remove(path));
            }

#ifdef __linux__
            SECTION("abstract") {
                auto listener = asyncio::net::UnixListener::listen("@asyncio-test.sock");
                REQUIRE(listener);

                co_await allSettled(
                    [](auto l) -> asyncio::task::Task<void> {
                        auto stream = co_await l.accept();
                        REQUIRE(stream);

#if UV_VERSION_MAJOR == 1 && UV_VERSION_MINOR >= 48
                        const auto localAddress = stream->localAddress();
                        REQUIRE(localAddress);
                        REQUIRE(fmt::to_string(*localAddress) == "variant(@asyncio-test.sock)");
#endif

                        auto res = co_await stream->writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(res);

                        std::string message;
                        message.resize(MESSAGE.size());

                        res = co_await stream->readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == MESSAGE);
                    }(*std::move(listener)),
                    []() -> asyncio::task::Task<void> {
                        auto stream = co_await asyncio::net::UnixStream::connect("@asyncio-test.sock");
                        REQUIRE(stream);

#if UV_VERSION_MAJOR == 1 && UV_VERSION_MINOR >= 48
                        const auto remoteAddress = stream->remoteAddress();
                        REQUIRE(remoteAddress);
                        REQUIRE(fmt::to_string(*remoteAddress) == "variant(@asyncio-test.sock)");
#endif

                        std::string message;
                        message.resize(MESSAGE.size());

                        auto res = co_await stream->readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message == MESSAGE);

                        res = co_await stream->writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(res);
                    }()
                );
            }
#endif
        }
#endif
    });
    REQUIRE(result);
    REQUIRE(*result);
}

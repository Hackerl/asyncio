#include <asyncio/io.h>
#include <asyncio/stream.h>
#include <catch2/catch_test_macros.hpp>

constexpr std::string_view MESSAGE = "hello world";

TEST_CASE("asynchronous io", "[io]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        SECTION("copy") {
            auto streams1 = asyncio::Stream::pair();
            REQUIRE(streams1);

            auto streams2 = asyncio::Stream::pair();
            REQUIRE(streams2);

            co_await allSettled(
                [](auto reader, auto writer) -> asyncio::task::Task<void> {
                    const auto res = co_await asyncio::copy(reader, writer);
                    REQUIRE(res);
                }(std::move(streams1->at(1)), std::move(streams2->at(0))),
                [](auto writer) -> asyncio::task::Task<void> {
                    auto res = co_await writer.writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(res);
                }(std::move(streams1->at(0))),
                [](auto reader) -> asyncio::task::Task<void> {
                    std::string message;
                    message.resize(MESSAGE.size());

                    auto res = co_await reader.readExactly(std::as_writable_bytes(std::span{message}));
                    REQUIRE(res);
                    REQUIRE(message == MESSAGE);

                    res = co_await reader.readExactly(std::as_writable_bytes(std::span{message}));
                    REQUIRE_FALSE(res);
                    REQUIRE(res.error() == asyncio::IOError::UNEXPECTED_EOF);
                }(std::move(streams2->at(1)))
            );
        }

        SECTION("copy bidirectional") {
            auto streams1 = asyncio::Stream::pair();
            REQUIRE(streams1);

            auto streams2 = asyncio::Stream::pair();
            REQUIRE(streams2);

            co_await allSettled(
                [](auto first, auto second) -> asyncio::task::Task<void> {
                    const auto res = co_await asyncio::copyBidirectional(first, second);
                    REQUIRE(res);
                }(std::move(streams1->at(1)), std::move(streams2->at(0))),
                [](auto stream) -> asyncio::task::Task<void> {
                    auto res = co_await stream.writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(res);

                    std::string message;
                    message.resize(MESSAGE.size());

                    res = co_await stream.readExactly(std::as_writable_bytes(std::span{message}));
                    REQUIRE(res);
                    REQUIRE(message == MESSAGE);

                    res = co_await stream.readExactly(std::as_writable_bytes(std::span{message}));
                    REQUIRE_FALSE(res);
                    REQUIRE(res.error() == asyncio::IOError::UNEXPECTED_EOF);
                }(std::move(streams1->at(0))),
                [](auto stream) -> asyncio::task::Task<void> {
                    std::string message;
                    message.resize(MESSAGE.size());

                    auto res = co_await stream.readExactly(std::as_writable_bytes(std::span{message}));
                    REQUIRE(res);
                    REQUIRE(message == MESSAGE);

                    res = co_await stream.writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(res);
                }(std::move(streams2->at(1)))
            );
        }

        SECTION("read all") {
            auto streams = asyncio::Stream::pair();
            REQUIRE(streams);

            co_await allSettled(
                [](auto stream) -> asyncio::task::Task<void> {
                    auto res = co_await stream.writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(res);

                    res = co_await stream.writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(res);
                }(std::move(streams->at(0))),
                [](auto reader) -> asyncio::task::Task<void> {
                    const auto res = co_await reader.readAll();
                    REQUIRE(res);
                    REQUIRE(res->size() == MESSAGE.size() * 2);
                    REQUIRE(memcmp(res->data(), MESSAGE.data(), MESSAGE.size()) == 0);
                    REQUIRE(memcmp(res->data() + MESSAGE.size(), MESSAGE.data(), MESSAGE.size()) == 0);
                }(std::move(streams->at(1)))
            );
        }

        SECTION("read exactly") {
            SECTION("normal") {
                auto streams = asyncio::Stream::pair();
                REQUIRE(streams);

                co_await allSettled(
                    [](auto writer) -> asyncio::task::Task<void> {
                        auto res = co_await writer.writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(res);

                        res = co_await writer.writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(res);

                        res = co_await writer.writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(res);
                    }(std::move(streams->at(0))),
                    [](auto reader) -> asyncio::task::Task<void> {
                        std::string message;
                        message.resize(MESSAGE.size() * 3);

                        const auto res = co_await reader.readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE(res);
                        REQUIRE(message.substr(0, MESSAGE.size()) == MESSAGE);
                        REQUIRE(message.substr(MESSAGE.size(), MESSAGE.size()) == MESSAGE);
                        REQUIRE(message.substr(MESSAGE.size() * 2, MESSAGE.size()) == MESSAGE);
                    }(std::move(streams->at(1)))
                );
            }

            SECTION("error") {
                auto streams = asyncio::Stream::pair();
                REQUIRE(streams);

                co_await allSettled(
                    [](auto writer) -> asyncio::task::Task<void> {
                        auto res = co_await writer.writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(res);

                        res = co_await writer.writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(res);
                    }(std::move(streams->at(0))),
                    [](auto reader) -> asyncio::task::Task<void> {
                        std::string message;
                        message.resize(MESSAGE.size() * 3);

                        const auto res = co_await reader.readExactly(std::as_writable_bytes(std::span{message}));
                        REQUIRE_FALSE(res);
                        REQUIRE(res.error() == asyncio::IOError::UNEXPECTED_EOF);
                    }(std::move(streams->at(1)))
                );
            }
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

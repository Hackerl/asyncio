#include <asyncio/io.h>
#include <asyncio/ev/pipe.h>
#include <asyncio/event_loop.h>
#include <asyncio/error.h>
#include <zero/strings/strings.h>
#include <catch2/catch_test_macros.hpp>

constexpr std::string_view MESSAGE = "hello world\r\n";

TEST_CASE("asynchronous io", "[io]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("copy") {
            auto buffers1 = asyncio::ev::pipe();
            REQUIRE(buffers1);

            auto buffers2 = asyncio::ev::pipe();
            REQUIRE(buffers2);

            co_await allSettled(
                [](auto reader, auto writer) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await asyncio::copy(reader, writer);
                    REQUIRE(result);
                }(std::move(buffers1->at(1)), std::move(buffers2->at(0))),
                [](auto buffer) -> zero::async::coroutine::Task<void> {
                    auto result = co_await buffer.writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(result);

                    result = co_await buffer.flush();
                    REQUIRE(result);
                }(std::move(buffers1->at(0))),
                [](auto buffer) -> zero::async::coroutine::Task<void> {
                    auto result = co_await buffer.readLine();
                    REQUIRE(result);
                    REQUIRE(*result == zero::strings::trim(MESSAGE));

                    result = co_await buffer.readLine();
                    REQUIRE(!result);
                    REQUIRE(result.error() == asyncio::Error::IO_EOF);
                }(std::move(buffers2->at(1)))
            );
        }

        SECTION("read all") {
            auto buffers = asyncio::ev::pipe();
            REQUIRE(buffers);

            co_await allSettled(
                [](auto buffer) -> zero::async::coroutine::Task<void> {
                    auto result = co_await buffer.writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(result);

                    result = co_await buffer.flush();
                    REQUIRE(result);

                    result = co_await buffer.writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(result);

                    result = co_await buffer.flush();
                    REQUIRE(result);
                }(std::move(buffers->at(0))),
                [](auto buffer) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await buffer.readAll();
                    REQUIRE(result);
                    REQUIRE(result->size() == MESSAGE.size() * 2);
                    REQUIRE(memcmp(result->data(), MESSAGE.data(), MESSAGE.size()) == 0);
                    REQUIRE(memcmp(result->data() + MESSAGE.size(), MESSAGE.data(), MESSAGE.size()) == 0);
                }(std::move(buffers->at(1)))
            );
        }

        SECTION("read exactly") {
            SECTION("normal") {
                auto buffers = asyncio::ev::pipe();
                REQUIRE(buffers);

                co_await allSettled(
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        auto result = co_await buffer.writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(result);

                        result = co_await buffer.flush();
                        REQUIRE(result);

                        result = co_await buffer.writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(result);

                        result = co_await buffer.flush();
                        REQUIRE(result);

                        result = co_await buffer.writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(result);

                        result = co_await buffer.flush();
                        REQUIRE(result);
                    }(std::move(buffers->at(0))),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        std::byte data[MESSAGE.size() * 3];
                        const auto result = co_await buffer.readExactly(data);
                        REQUIRE(result);
                        REQUIRE(memcmp(data, MESSAGE.data(), MESSAGE.size()) == 0);
                        REQUIRE(memcmp(data + MESSAGE.size(), MESSAGE.data(), MESSAGE.size()) == 0);
                        REQUIRE(memcmp(data + MESSAGE.size() * 2, MESSAGE.data(), MESSAGE.size()) == 0);
                    }(std::move(buffers->at(1)))
                );
            }

            SECTION("error") {
                auto buffers = asyncio::ev::pipe();
                REQUIRE(buffers);

                co_await allSettled(
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        auto result = co_await buffer.writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(result);

                        result = co_await buffer.flush();
                        REQUIRE(result);

                        result = co_await buffer.writeAll(std::as_bytes(std::span{MESSAGE}));
                        REQUIRE(result);

                        result = co_await buffer.flush();
                        REQUIRE(result);
                    }(std::move(buffers->at(0))),
                    [](auto buffer) -> zero::async::coroutine::Task<void> {
                        std::byte data[MESSAGE.size() * 3];
                        const auto result = co_await buffer.readExactly(data);
                        REQUIRE(!result);
                        REQUIRE(result.error() == asyncio::Error::IO_EOF);
                    }(std::move(buffers->at(1)))
                );
            }
        }
    });
}

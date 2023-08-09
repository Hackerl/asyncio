#include <asyncio/channel.h>
#include <asyncio/thread.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("async channel buffer", "[channel]") {
    std::atomic<int> counters[2] = {};

    SECTION("async sender/async receiver") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            asyncio::Channel<int> channel(100);

            auto produce = [&]() -> zero::async::coroutine::Task<void> {
                while (true) {
                    if (counters[0] >= 100000)
                        break;

                    auto result = co_await channel.send(counters[0]++);
                    REQUIRE(result);
                }
            };

            auto consume = [&]() -> zero::async::coroutine::Task<void, std::error_code> {
                tl::expected<void, std::error_code> result;

                while (true) {
                    auto res = co_await channel.receive();

                    if (!res) {
                        result = tl::unexpected(res.error());
                        break;
                    }

                    counters[1]++;
                }

                co_return result;
            };

            co_await zero::async::coroutine::allSettled(
                    [&]() -> zero::async::coroutine::Task<void> {
                        auto result = co_await zero::async::coroutine::allSettled(produce(), produce());
                        REQUIRE(result);
                        channel.close();
                    }(),
                    [&]() -> zero::async::coroutine::Task<void> {
                        auto result = co_await zero::async::coroutine::allSettled(consume(), consume());
                        REQUIRE(std::get<0>(*result).error() == asyncio::Error::IO_EOF);
                        REQUIRE(std::get<1>(*result).error() == asyncio::Error::IO_EOF);
                    }()
            );

            REQUIRE(counters[0] == counters[1]);
        });
    }

    SECTION("sync sender/async receiver") {
        SECTION("normal") {
            asyncio::run([&]() -> zero::async::coroutine::Task<void> {
                asyncio::Channel<int> channel(100);

                auto produce = [&]() {
                    while (true) {
                        if (counters[0] >= 100000)
                            break;

                        channel.sendSync(counters[0]++);
                    }
                };

                auto consume = [&]() -> zero::async::coroutine::Task<void, std::error_code> {
                    tl::expected<void, std::error_code> result;

                    while (true) {
                        auto res = co_await channel.receive();

                        if (!res) {
                            result = tl::unexpected(res.error());
                            break;
                        }

                        counters[1]++;
                    }

                    co_return result;
                };

                co_await zero::async::coroutine::allSettled(
                        [&]() -> zero::async::coroutine::Task<void> {
                            auto result = co_await zero::async::coroutine::allSettled(
                                    asyncio::toThread(produce),
                                    asyncio::toThread(produce)
                            );

                            REQUIRE(result);
                            channel.close();
                        }(),
                        [&]() -> zero::async::coroutine::Task<void> {
                            auto result = co_await zero::async::coroutine::allSettled(consume(), consume());
                            REQUIRE(std::get<0>(*result).error() == asyncio::Error::IO_EOF);
                            REQUIRE(std::get<1>(*result).error() == asyncio::Error::IO_EOF);
                        }()
                );

                REQUIRE(counters[0] == counters[1]);
            });
        }

        SECTION("send timeout") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                asyncio::Channel<int> channel(2);

                co_await asyncio::toThread([&]() {
                    auto result = channel.sendSync(0, 50ms);
                    REQUIRE(result);

                    result = channel.sendSync(0, 50ms);

                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::timed_out);
                });
            });
        }

        SECTION("receive timeout") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                asyncio::Channel<int> channel(2);
                auto result = co_await channel.receive(50ms);

                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::timed_out);
            });
        }
    }

    SECTION("async sender/sync receiver") {
        SECTION("normal") {
            asyncio::run([&]() -> zero::async::coroutine::Task<void> {
                asyncio::Channel<int> channel(100);

                auto produce = [&]() -> zero::async::coroutine::Task<void> {
                    while (true) {
                        if (counters[0] >= 100000)
                            break;

                        auto result = co_await channel.send(counters[0]++);
                        REQUIRE(result);
                    }
                };

                auto consume = [&]() {
                    tl::expected<void, std::error_code> result;

                    while (true) {
                        auto res = channel.receiveSync();

                        if (!res) {
                            result = tl::unexpected(res.error());
                            break;
                        }

                        counters[1]++;
                    }

                    return result;
                };

                co_await zero::async::coroutine::allSettled(
                        [&]() -> zero::async::coroutine::Task<void> {
                            auto result = co_await zero::async::coroutine::allSettled(produce(), produce());
                            REQUIRE(result);
                            channel.close();
                        }(),
                        [&]() -> zero::async::coroutine::Task<void> {
                            auto result = co_await zero::async::coroutine::allSettled(
                                    asyncio::toThread(consume),
                                    asyncio::toThread(consume)
                            );

                            REQUIRE(std::get<0>(*result).error() == asyncio::Error::IO_EOF);
                            REQUIRE(std::get<1>(*result).error() == asyncio::Error::IO_EOF);
                        }()
                );

                REQUIRE(counters[0] == counters[1]);
            });
        }

        SECTION("send timeout") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                asyncio::Channel<int> channel(2);

                auto result = co_await channel.send(0, 50ms);
                REQUIRE(result);

                result = co_await channel.send(0, 50ms);

                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::timed_out);
            });
        }

        SECTION("receive timeout") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                asyncio::Channel<int> channel(2);

                co_await asyncio::toThread([&]() {
                    auto result = channel.receiveSync(50ms);

                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::timed_out);
                });
            });
        }
    }
}
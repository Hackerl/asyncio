#include <asyncio/channel.h>
#include <asyncio/thread.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

zero::async::coroutine::Task<void>
produce(std::shared_ptr<asyncio::ISender<int>> sender, std::shared_ptr<std::atomic<int>> counter) {
    while (true) {
        if (*counter >= 10000)
            break;

        auto result = co_await sender->send((*counter)++);
        REQUIRE(result);
    }
}

tl::expected<void, std::error_code>
produceSync(const std::shared_ptr<asyncio::ISender<int>> &sender, const std::shared_ptr<std::atomic<int>> &counter) {
    while (true) {
        if (*counter >= 10000)
            break;

        sender->sendSync((*counter)++);
    }

    return {};
}

zero::async::coroutine::Task<void, std::error_code>
consume(std::shared_ptr<asyncio::IReceiver<int>> receiver, std::shared_ptr<std::atomic<int>> counter) {
    tl::expected<void, std::error_code> result;

    while (true) {
        auto res = co_await receiver->receive();

        if (!res) {
            result = tl::unexpected(res.error());
            break;
        }

        (*counter)++;
    }

    co_return result;
}

tl::expected<void, std::error_code> consumeSync(
        const std::shared_ptr<asyncio::IReceiver<int>> &receiver,
        const std::shared_ptr<std::atomic<int>> &counter
) {
    tl::expected<void, std::error_code> result;

    while (true) {
        auto res = receiver->receiveSync();

        if (!res) {
            result = tl::unexpected(res.error());
            break;
        }

        (*counter)++;
    }

    return result;
}

TEST_CASE("async channel buffer", "[channel]") {
    std::shared_ptr<std::atomic<int>> counters[2] = {
            std::make_shared<std::atomic<int>>(),
            std::make_shared<std::atomic<int>>()
    };

    SECTION("async sender/async receiver") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto channel = std::make_shared<asyncio::Channel<int>>(100);

            co_await zero::async::coroutine::allSettled(
                    [](auto channel, auto counter) -> zero::async::coroutine::Task<void> {
                        auto result = co_await zero::async::coroutine::allSettled(
                                produce(channel, counter),
                                produce(channel, counter)
                        );

                        REQUIRE(result);
                        channel->close();
                    }(channel, counters[0]),
                    [](auto channel, auto counter) -> zero::async::coroutine::Task<void> {
                        auto result = co_await zero::async::coroutine::allSettled(
                                consume(channel, counter),
                                consume(channel, counter)
                        );

                        REQUIRE(std::get<0>(*result).error() == asyncio::Error::IO_EOF);
                        REQUIRE(std::get<1>(*result).error() == asyncio::Error::IO_EOF);
                    }(channel, counters[1])
            );

            REQUIRE(*counters[0] == *counters[1]);
        });
    }

    SECTION("sync sender/async receiver") {
        SECTION("normal") {
            asyncio::run([&]() -> zero::async::coroutine::Task<void> {
                auto channel = std::make_shared<asyncio::Channel<int>>(100);

                co_await zero::async::coroutine::allSettled(
                        [](auto channel, auto counter) -> zero::async::coroutine::Task<void> {
                            auto result = co_await zero::async::coroutine::allSettled(
                                    asyncio::toThread([=] { return produceSync(channel, counter); }),
                                    asyncio::toThread([=] { return produceSync(channel, counter); })
                            );

                            REQUIRE(result);
                            channel->close();
                        }(channel, counters[0]),
                        [](auto channel, auto counter) -> zero::async::coroutine::Task<void> {
                            auto result = co_await zero::async::coroutine::allSettled(
                                    consume(channel, counter),
                                    consume(channel, counter)
                            );

                            REQUIRE(std::get<0>(*result).error() == asyncio::Error::IO_EOF);
                            REQUIRE(std::get<1>(*result).error() == asyncio::Error::IO_EOF);
                        }(channel, counters[1])
                );

                REQUIRE(*counters[0] == *counters[1]);
            });
        }

        SECTION("send timeout") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto channel = std::make_shared<asyncio::Channel<int>>(2);

                co_await asyncio::toThread([=]() -> tl::expected<void, std::error_code> {
                    auto result = channel->sendSync(0, 50ms);
                    REQUIRE(result);

                    result = channel->sendSync(0, 50ms);

                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::timed_out);

                    return {};
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
                auto channel = std::make_shared<asyncio::Channel<int>>(100);

                co_await zero::async::coroutine::allSettled(
                        [](auto channel, auto counter) -> zero::async::coroutine::Task<void> {
                            auto result = co_await zero::async::coroutine::allSettled(
                                    produce(channel, counter),
                                    produce(channel, counter)
                            );

                            REQUIRE(result);
                            channel->close();
                        }(channel, counters[0]),
                        [](auto channel, auto counter) -> zero::async::coroutine::Task<void> {
                            auto result = co_await zero::async::coroutine::allSettled(
                                    asyncio::toThread([=] { return consumeSync(channel, counter); }),
                                    asyncio::toThread([=] { return consumeSync(channel, counter); })
                            );

                            REQUIRE(std::get<0>(*result).error() == asyncio::Error::IO_EOF);
                            REQUIRE(std::get<1>(*result).error() == asyncio::Error::IO_EOF);
                        }(channel, counters[1])
                );

                REQUIRE(*counters[0] == *counters[1]);
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
                auto channel = std::make_shared<asyncio::Channel<int>>(2);

                co_await asyncio::toThread([=]() -> tl::expected<void, std::error_code> {
                    auto result = channel->receiveSync(50ms);

                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::timed_out);

                    return {};
                });
            });
        }
    }
}
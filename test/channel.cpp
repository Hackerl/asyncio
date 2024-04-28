#include <asyncio/channel.h>
#include <asyncio/thread.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

zero::async::coroutine::Task<void>
produce(const asyncio::SenderPtr<int> sender, const std::shared_ptr<std::atomic<int>> counter) {
    while (true) {
        if (*counter >= 10000)
            break;

        const auto result = co_await sender->send((*counter)++);
        REQUIRE(result);
    }
}

tl::expected<void, std::error_code>
produceSync(const asyncio::SenderPtr<int> &sender, const std::shared_ptr<std::atomic<int>> &counter) {
    while (true) {
        if (*counter >= 10000)
            break;

        sender->sendSync((*counter)++);
    }

    return {};
}

zero::async::coroutine::Task<void, std::error_code>
consume(const asyncio::ReceiverPtr<int> receiver, const std::shared_ptr<std::atomic<int>> counter) {
    while (true) {
        CO_EXPECT(co_await receiver->receive());
        ++*counter;
    }
}

tl::expected<void, std::error_code>
consumeSync(const asyncio::ReceiverPtr<int> &receiver, const std::shared_ptr<std::atomic<int>> &counter) {
    tl::expected<void, std::error_code> result;

    while (true) {
        if (const auto res = receiver->receiveSync(); !res) {
            result = tl::unexpected(res.error());
            break;
        }

        ++*counter;
    }

    return result;
}

TEST_CASE("async channel buffer", "[channel]") {
    const std::shared_ptr<std::atomic<int>> counters[2] = {
        std::make_shared<std::atomic<int>>(),
        std::make_shared<std::atomic<int>>()
    };

    asyncio::run([&]() -> zero::async::coroutine::Task<void> {
        SECTION("async sender/async receiver") {
            const auto channel = asyncio::Channel<int>::make(100);

            co_await allSettled(
                [](auto ch, auto ct) -> zero::async::coroutine::Task<void> {
                    co_await zero::async::coroutine::allSettled(produce(ch, ct), produce(ch, ct));
                    ch->close();
                }(channel, counters[0]),
                [](auto ch, auto ct) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await zero::async::coroutine::allSettled(
                        consume(ch, ct),
                        consume(ch, ct)
                    );
                    REQUIRE(std::get<0>(result).error() == asyncio::ChannelError::CHANNEL_EOF);
                    REQUIRE(std::get<1>(result).error() == asyncio::ChannelError::CHANNEL_EOF);
                }(channel, counters[1])
            );

            REQUIRE(*counters[0] == *counters[1]);
        }

        SECTION("sync sender/async receiver") {
            SECTION("normal") {
                const auto channel = asyncio::Channel<int>::make(100);

                co_await allSettled(
                    [](auto ch, auto ct) -> zero::async::coroutine::Task<void> {
                        co_await zero::async::coroutine::allSettled(
                            asyncio::toThread([=] { return produceSync(ch, ct); }),
                            asyncio::toThread([=] { return produceSync(ch, ct); })
                        );

                        ch->close();
                    }(channel, counters[0]),
                    [](auto ch, auto ct) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await zero::async::coroutine::allSettled(
                            consume(ch, ct),
                            consume(ch, ct)
                        );

                        REQUIRE(std::get<0>(result).error() == asyncio::ChannelError::CHANNEL_EOF);
                        REQUIRE(std::get<1>(result).error() == asyncio::ChannelError::CHANNEL_EOF);
                    }(channel, counters[1])
                );

                REQUIRE(*counters[0] == *counters[1]);
            }

            SECTION("send timeout") {
                const auto channel = asyncio::Channel<int>::make(2);

                co_await asyncio::toThread([=]() -> tl::expected<void, std::error_code> {
                    auto result = channel->sendSync(0, 20ms);
                    REQUIRE(result);

                    result = channel->sendSync(0, 20ms);

                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::timed_out);

                    return {};
                });
            }

            SECTION("receive timeout") {
                asyncio::Channel<int> channel(2);
                const auto result = co_await channel.receive(20ms);
                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::timed_out);
            }
        }

        SECTION("async sender/sync receiver") {
            SECTION("normal") {
                const auto channel = asyncio::Channel<int>::make(100);

                co_await allSettled(
                    [](auto ch, auto ct) -> zero::async::coroutine::Task<void> {
                        co_await zero::async::coroutine::allSettled(produce(ch, ct), produce(ch, ct));
                        ch->close();
                    }(channel, counters[0]),
                    [](auto ch, auto ct) -> zero::async::coroutine::Task<void> {
                        const auto result = co_await zero::async::coroutine::allSettled(
                            asyncio::toThread([=] { return consumeSync(ch, ct); }),
                            asyncio::toThread([=] { return consumeSync(ch, ct); })
                        );

                        REQUIRE(std::get<0>(result).error() == asyncio::ChannelError::CHANNEL_EOF);
                        REQUIRE(std::get<1>(result).error() == asyncio::ChannelError::CHANNEL_EOF);
                    }(channel, counters[1])
                );

                REQUIRE(*counters[0] == *counters[1]);
            }

            SECTION("send timeout") {
                asyncio::Channel<int> channel(2);

                auto result = co_await channel.send(0, 20ms);
                REQUIRE(result);

                result = co_await channel.send(0, 20ms);
                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::timed_out);
            }

            SECTION("receive timeout") {
                const auto channel = asyncio::Channel<int>::make(2);

                co_await asyncio::toThread([=]() -> tl::expected<void, std::error_code> {
                    const auto result = channel->receiveSync(20ms);

                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::timed_out);

                    return {};
                });
            }
        }
    });
}

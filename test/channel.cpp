#include <asyncio/channel.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

zero::async::coroutine::Task<void, std::error_code>
produce(asyncio::Sender<int> sender, const std::shared_ptr<std::atomic<int>> counter) {
    while (true) {
        if (*counter >= 10000)
            break;

        CO_EXPECT(co_await sender.send((*counter)++));
    }

    co_return tl::expected<void, std::error_code>{};
}

tl::expected<void, std::error_code>
produceSync(asyncio::Sender<int> sender, const std::shared_ptr<std::atomic<int>> &counter) {
    while (true) {
        if (*counter >= 10000)
            break;

        sender.sendSync((*counter)++);
    }

    return {};
}

zero::async::coroutine::Task<void, std::error_code>
consume(asyncio::Receiver<int> receiver, const std::shared_ptr<std::atomic<int>> counter) {
    while (true) {
        CO_EXPECT(co_await receiver.receive());
        ++*counter;
    }
}

tl::expected<void, std::error_code>
consumeSync(asyncio::Receiver<int> receiver, const std::shared_ptr<std::atomic<int>> &counter) {
    while (true) {
        EXPECT(receiver.receiveSync());
        ++*counter;
    }
}

TEST_CASE("async channel buffer", "[channel]") {
    const std::shared_ptr<std::atomic<int>> counters[2] = {
        std::make_shared<std::atomic<int>>(),
        std::make_shared<std::atomic<int>>()
    };

    asyncio::run([&]() -> zero::async::coroutine::Task<void> {
        SECTION("async sender/async receiver") {
            auto [sender, receiver] = asyncio::channel<int>(100);

            co_await allSettled(
                [](auto s, auto ct) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await zero::async::coroutine::allSettled(produce(s, ct), produce(s, ct));
                    REQUIRE(result[0]);
                    REQUIRE(result[1]);
                }(std::move(sender), counters[0]),
                [](auto r, auto ct) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await zero::async::coroutine::allSettled(
                        consume(r, ct),
                        consume(r, ct)
                    );
                    REQUIRE(result[0].error() == asyncio::ReceiveError::DISCONNECTED);
                    REQUIRE(result[1].error() == asyncio::ReceiveError::DISCONNECTED);
                }(std::move(receiver), counters[1])
            );

            REQUIRE(*counters[0] == *counters[1]);
        }

        SECTION("sync sender/async receiver") {
            auto [sender, receiver] = asyncio::channel<int>(100);

            co_await allSettled(
                [](auto s, auto ct) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await zero::async::coroutine::allSettled(
                        asyncio::toThread([=] { return produceSync(s, ct); }),
                        asyncio::toThread([=] { return produceSync(s, ct); })
                    );
                    REQUIRE(result[0]);
                    REQUIRE(result[1]);
                }(std::move(sender), counters[0]),
                [](auto r, auto ct) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await zero::async::coroutine::allSettled(
                        consume(r, ct),
                        consume(r, ct)
                    );
                    REQUIRE(result[0].error() == asyncio::ReceiveError::DISCONNECTED);
                    REQUIRE(result[1].error() == asyncio::ReceiveError::DISCONNECTED);
                }(std::move(receiver), counters[1])
            );
            REQUIRE(*counters[0] == *counters[1]);
        }

        SECTION("async sender/sync receiver") {
            auto [sender, receiver] = asyncio::channel<int>(100);

            co_await allSettled(
                [](auto s, auto ct) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await zero::async::coroutine::allSettled(produce(s, ct), produce(s, ct));
                    REQUIRE(result[0]);
                    REQUIRE(result[1]);
                }(std::move(sender), counters[0]),
                [](auto r, auto ct) -> zero::async::coroutine::Task<void> {
                    const auto result = co_await zero::async::coroutine::allSettled(
                        asyncio::toThread([=] { return consumeSync(r, ct); }),
                        asyncio::toThread([=] { return consumeSync(r, ct); })
                    );
                    REQUIRE(result[0].error() == asyncio::ReceiveSyncError::DISCONNECTED);
                    REQUIRE(result[1].error() == asyncio::ReceiveSyncError::DISCONNECTED);
                }(std::move(receiver), counters[1])
            );

            REQUIRE(*counters[0] == *counters[1]);
        }

        SECTION("timeout") {
            auto [sender, receiver] = asyncio::channel<int>(2);

            {
                const auto result = co_await timeout(receiver.receive(), 20ms);
                REQUIRE(!result);
                REQUIRE(result.error() == asyncio::TimeoutError::ELAPSED);
            }

            {
                const auto result = co_await asyncio::toThread([&] {
                    return receiver.receiveSync(20ms);
                });
                REQUIRE(!result);
                REQUIRE(result.error() == asyncio::ReceiveSyncError::TIMEOUT);
            }

            sender.trySend(0);
            REQUIRE(sender.full());

            {
                const auto result = co_await timeout(sender.send(0), 20ms);
                REQUIRE(!result);
                REQUIRE(result.error() == asyncio::TimeoutError::ELAPSED);
            }

            {
                const auto result = co_await asyncio::toThread([&] {
                    return sender.sendSync(0, 20ms);
                });
                REQUIRE(!result);
                REQUIRE(result.error() == asyncio::SendSyncError::TIMEOUT);
            }
        }
    });
}

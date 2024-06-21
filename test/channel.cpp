#include <asyncio/channel.h>
#include <asyncio/time.h>
#include <asyncio/thread.h>
#include <catch2/catch_test_macros.hpp>
#include <future>

constexpr auto MAX_COUNT = 100000;

asyncio::task::Task<void, std::error_code>
produce(asyncio::Sender<std::string> sender, const std::shared_ptr<std::atomic<int>> counter) {
    while (true) {
        if (*counter >= MAX_COUNT)
            co_return {};

        CO_EXPECT(co_await sender.send("hello world"));
        ++*counter;
    }
}

std::expected<void, std::error_code>
produceSync(asyncio::Sender<std::string> sender, const std::shared_ptr<std::atomic<int>> &counter) {
    while (true) {
        if (*counter >= MAX_COUNT)
            return {};

        EXPECT(sender.sendSync("hello world"));
        ++*counter;
    }
}

asyncio::task::Task<void, std::error_code>
consume(asyncio::Receiver<std::string> receiver, const std::shared_ptr<std::atomic<int>> counter) {
    while (true) {
        if (const auto result = co_await receiver.receive(); !result) {
            if (result.error() != asyncio::ReceiveError::DISCONNECTED)
                co_return std::unexpected(result.error());

            co_return {};
        }

        ++*counter;
    }
}

std::expected<void, std::error_code>
consumeSync(asyncio::Receiver<std::string> receiver, const std::shared_ptr<std::atomic<int>> &counter) {
    while (true) {
        if (const auto result = receiver.receiveSync(); !result) {
            if (result.error() != asyncio::ReceiveSyncError::DISCONNECTED)
                return std::unexpected(result.error());

            return {};
        }

        ++*counter;
    }
}

TEST_CASE("asyncio channel", "[channel]") {
    const auto result = asyncio::run([&]() -> asyncio::task::Task<void> {
        const std::array counters = {
            std::make_shared<std::atomic<int>>(),
            std::make_shared<std::atomic<int>>()
        };

        SECTION("normal") {
            using namespace std::chrono_literals;
            auto [sender, receiver] = asyncio::channel<std::string>(100);

            std::array futures = {
                std::async(produceSync, sender, counters[0]),
                std::async(produceSync, sender, counters[0]),
                std::async(produceSync, sender, counters[0]),
                std::async(consumeSync, receiver, counters[1]),
                std::async(consumeSync, receiver, counters[1])
            };

            const auto res = co_await all(
                produce(std::move(sender), counters[0]),
                consume(receiver, counters[1]),
                consume(receiver, counters[1])
            );
            REQUIRE(res);

            for (auto &future: futures) {
                const auto r = co_await asyncio::toThread([&] {
                    return future.get();
                });
                REQUIRE(r);
                REQUIRE(*r);
            }

            REQUIRE(*counters[0] == *counters[1]);
        }

        SECTION("timeout") {
            auto [sender, receiver] = asyncio::channel<std::string>(2);

            SECTION("receive") {
                using namespace std::chrono_literals;
                const auto res = co_await timeout(receiver.receive(), 20ms);
                REQUIRE(!res);
                REQUIRE(res.error() == asyncio::TimeoutError::ELAPSED);
            }

            SECTION("receiveSync") {
                using namespace std::chrono_literals;
                const auto res = receiver.receiveSync(20ms);
                REQUIRE(!res);
                REQUIRE(res.error() == asyncio::ReceiveSyncError::TIMEOUT);
            }

            REQUIRE(sender.trySend("hello world"));
            REQUIRE(sender.full());

            SECTION("send") {
                using namespace std::chrono_literals;
                const auto res = co_await timeout(sender.send("hello world"), 20ms);
                REQUIRE(!res);
                REQUIRE(res.error() == asyncio::TimeoutError::ELAPSED);
            }

            SECTION("sendSync") {
                using namespace std::chrono_literals;
                const auto res = sender.sendSync("hello world", 20ms);
                REQUIRE(!res);
                REQUIRE(res.error() == asyncio::SendSyncError::TIMEOUT);
            }
        }
    });
    REQUIRE(result);
    REQUIRE(*result);

    SECTION("error condition") {
        const std::error_condition condition = asyncio::ChannelError::DISCONNECTED;
        REQUIRE(condition == asyncio::TrySendError::DISCONNECTED);
        REQUIRE(condition == asyncio::SendSyncError::DISCONNECTED);
        REQUIRE(condition == asyncio::SendError::DISCONNECTED);
        REQUIRE(condition == asyncio::TryReceiveError::DISCONNECTED);
        REQUIRE(condition == asyncio::ReceiveSyncError::DISCONNECTED);
        REQUIRE(condition == asyncio::ReceiveError::DISCONNECTED);
    }
}

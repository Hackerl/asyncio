#include "catch_extensions.h"
#include <asyncio/channel.h>
#include <asyncio/thread.h>
#include <future>

constexpr auto MAX_COUNT = 100000;
constexpr auto MESSAGE = "hello world";

asyncio::task::Task<void, std::error_code>
produce(asyncio::Sender<std::string> sender, const std::shared_ptr<std::atomic<int>> counter) {
    while (true) {
        if (*counter >= MAX_COUNT)
            co_return {};

        CO_EXPECT(co_await sender.send(MESSAGE));
        ++*counter;
    }
}

std::expected<void, std::error_code>
produceSync(asyncio::Sender<std::string> sender, const std::shared_ptr<std::atomic<int>> &counter) {
    while (true) {
        if (*counter >= MAX_COUNT)
            return {};

        EXPECT(sender.sendSync(MESSAGE));
        ++*counter;
    }
}

asyncio::task::Task<void, std::error_code>
consume(asyncio::Receiver<std::string> receiver, const std::shared_ptr<std::atomic<int>> counter) {
    while (true) {
        const auto result = co_await receiver.receive();

        if (!result) {
            if (result.error() != asyncio::ReceiveError::DISCONNECTED)
                co_return std::unexpected{result.error()};

            co_return {};
        }

        if (*result != MESSAGE)
            co_return std::unexpected{make_error_code(std::errc::bad_message)};

        ++*counter;
    }
}

std::expected<void, std::error_code>
consumeSync(asyncio::Receiver<std::string> receiver, const std::shared_ptr<std::atomic<int>> &counter) {
    while (true) {
        const auto result = receiver.receiveSync();

        if (!result) {
            if (result.error() != asyncio::ReceiveSyncError::DISCONNECTED)
                return std::unexpected{result.error()};

            return {};
        }

        if (*result != MESSAGE)
            return std::unexpected{make_error_code(std::errc::bad_message)};

        ++*counter;
    }
}

TEST_CASE("error condition", "[channel]") {
    const std::error_condition condition = asyncio::ChannelError::DISCONNECTED;
    REQUIRE(condition == asyncio::TrySendError::DISCONNECTED);
    REQUIRE(condition == asyncio::SendSyncError::DISCONNECTED);
    REQUIRE(condition == asyncio::SendError::DISCONNECTED);
    REQUIRE(condition == asyncio::TryReceiveError::DISCONNECTED);
    REQUIRE(condition == asyncio::ReceiveSyncError::DISCONNECTED);
    REQUIRE(condition == asyncio::ReceiveError::DISCONNECTED);
}

ASYNC_TEST_CASE("channel concurrency testing", "[channel]") {
    const std::array counters{
        std::make_shared<std::atomic<int>>(),
        std::make_shared<std::atomic<int>>()
    };

    auto [sender, receiver] = asyncio::channel<std::string>(100);

    std::array futures{
        std::async(produceSync, sender, counters[0]),
        std::async(produceSync, sender, counters[0]),
        std::async(produceSync, sender, counters[0]),
        std::async(consumeSync, receiver, counters[1]),
        std::async(consumeSync, receiver, counters[1])
    };

    const auto result = co_await all(
        produce(std::move(sender), counters[0]),
        consume(receiver, counters[1]),
        consume(receiver, counters[1])
    );
    REQUIRE(result);

    for (auto &future: futures) {
        REQUIRE(co_await asyncio::toThread([&] {
            return future.get();
        }));
    }

    REQUIRE(*counters[0] == *counters[1]);
}

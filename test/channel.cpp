#include "catch_extensions.h"
#include <asyncio/channel.h>
#include <asyncio/thread.h>
#include <asyncio/time.h>

TEST_CASE("channel error condition", "[channel]") {
    const std::error_condition condition{asyncio::ChannelError::DISCONNECTED};
    REQUIRE(condition == asyncio::TrySendError::DISCONNECTED);
    REQUIRE(condition == asyncio::SendSyncError::DISCONNECTED);
    REQUIRE(condition == asyncio::SendError::DISCONNECTED);
    REQUIRE(condition == asyncio::TryReceiveError::DISCONNECTED);
    REQUIRE(condition == asyncio::ReceiveSyncError::DISCONNECTED);
    REQUIRE(condition == asyncio::ReceiveError::DISCONNECTED);
}

TEST_CASE("channel try send error", "[channel]") {
    SECTION("disconnected") {
        const std::error_code ec{asyncio::TrySendError::DISCONNECTED};
        REQUIRE(ec == asyncio::ChannelError::DISCONNECTED);
    }

    SECTION("full") {
        const std::error_code ec{asyncio::TrySendError::FULL};
        REQUIRE(ec == std::errc::operation_would_block);
    }
}

TEST_CASE("channel send sync error", "[channel]") {
    SECTION("disconnected") {
        const std::error_code ec{asyncio::SendSyncError::DISCONNECTED};
        REQUIRE(ec == asyncio::ChannelError::DISCONNECTED);
    }

    SECTION("timeout") {
        const std::error_code ec{asyncio::SendSyncError::TIMEOUT};
        REQUIRE(ec == std::errc::timed_out);
    }
}

TEST_CASE("channel send error", "[channel]") {
    SECTION("disconnected") {
        const std::error_code ec{asyncio::SendError::DISCONNECTED};
        REQUIRE(ec == asyncio::ChannelError::DISCONNECTED);
    }

    SECTION("cancelled") {
        const std::error_code ec{asyncio::SendError::CANCELLED};
        REQUIRE(ec == std::errc::operation_canceled);
    }
}

TEST_CASE("channel try receive error", "[channel]") {
    SECTION("disconnected") {
        const std::error_code ec{asyncio::TryReceiveError::DISCONNECTED};
        REQUIRE(ec == asyncio::ChannelError::DISCONNECTED);
    }

    SECTION("empty") {
        const std::error_code ec{asyncio::TryReceiveError::EMPTY};
        REQUIRE(ec == std::errc::operation_would_block);
    }
}

TEST_CASE("channel receive sync error", "[channel]") {
    SECTION("disconnected") {
        const std::error_code ec{asyncio::ReceiveSyncError::DISCONNECTED};
        REQUIRE(ec == asyncio::ChannelError::DISCONNECTED);
    }

    SECTION("timeout") {
        const std::error_code ec{asyncio::ReceiveSyncError::TIMEOUT};
        REQUIRE(ec == std::errc::timed_out);
    }
}

TEST_CASE("channel receive error", "[channel]") {
    SECTION("disconnected") {
        const std::error_code ec{asyncio::ReceiveError::DISCONNECTED};
        REQUIRE(ec == asyncio::ChannelError::DISCONNECTED);
    }

    SECTION("cancelled") {
        const std::error_code ec{asyncio::ReceiveError::CANCELLED};
        REQUIRE(ec == std::errc::operation_canceled);
    }
}

ASYNC_TEST_CASE("channel sender", "[channel]") {
    const auto capacity = GENERATE(1uz, take(1, random(2uz, 1024uz)));
    const auto element = GENERATE(take(1, randomString(1, 1024)));

    auto [sender, receiver] = asyncio::channel<std::string>(capacity);

    SECTION("try send") {
        SECTION("success") {
            REQUIRE(sender.trySend(element));
        }

        SECTION("disconnected") {
            sender.close();
            REQUIRE_ERROR(sender.trySend(element), asyncio::TrySendError::DISCONNECTED);
        }

        SECTION("full") {
            for (std::size_t i{0}; i < capacity; ++i) {
                REQUIRE(sender.trySend(element));
            }

            REQUIRE_ERROR(sender.trySend(element), asyncio::TrySendError::FULL);
        }
    }

    SECTION("try send extended") {
        SECTION("success") {
            REQUIRE(sender.trySendEx(std::string{element}));
        }

        SECTION("disconnected") {
            sender.close();

            const auto result = sender.trySendEx(std::string{element});
            REQUIRE_FALSE(result);
            REQUIRE(std::get<0>(result.error()) == element);
            REQUIRE(std::get<1>(result.error()) == asyncio::TrySendError::DISCONNECTED);
        }

        SECTION("full") {
            for (std::size_t i{0}; i < capacity; ++i) {
                REQUIRE(sender.trySend(element));
            }

            const auto result = sender.trySendEx(std::string{element});
            REQUIRE_FALSE(result);
            REQUIRE(std::get<0>(result.error()) == element);
            REQUIRE(std::get<1>(result.error()) == asyncio::TrySendError::FULL);
        }
    }

    SECTION("send sync") {
        SECTION("success") {
            SECTION("no wait") {
                REQUIRE(sender.sendSync(element));
            }

            SECTION("wait") {
                for (std::size_t i{0}; i < capacity; ++i) {
                    REQUIRE(sender.trySend(element));
                }

                auto task = asyncio::toThread([&] {
                    return sender.sendSync(element);
                });
                REQUIRE(co_await receiver.receive() == element);
                REQUIRE(co_await task);
            }

            SECTION("wait with timeout") {
                using namespace std::chrono_literals;

                for (std::size_t i{0}; i < capacity; ++i) {
                    REQUIRE(sender.trySend(element));
                }

                auto task = asyncio::toThread([&] {
                    return sender.sendSync(element, 1s);
                });

                REQUIRE(co_await asyncio::sleep(10ms));
                REQUIRE(co_await receiver.receive() == element);
                REQUIRE(co_await task);
            }
        }

        SECTION("disconnected") {
            sender.close();
            REQUIRE_ERROR(sender.sendSync(element), asyncio::SendSyncError::DISCONNECTED);
        }

        SECTION("timeout") {
            using namespace std::chrono_literals;

            for (std::size_t i{0}; i < capacity; ++i) {
                REQUIRE(sender.trySend(element));
            }

            REQUIRE_ERROR(sender.sendSync(element, 10ms), asyncio::SendSyncError::TIMEOUT);
        }
    }

    SECTION("send sync extended") {
        SECTION("success") {
            SECTION("no wait") {
                REQUIRE(sender.sendSyncEx(std::string{element}));
            }

            SECTION("wait") {
                for (std::size_t i{0}; i < capacity; ++i) {
                    REQUIRE(sender.trySend(element));
                }

                auto task = asyncio::toThread([&] {
                    return sender.sendSyncEx(std::string{element});
                });
                REQUIRE(co_await receiver.receive() == element);
                REQUIRE(co_await task);
            }

            SECTION("wait with timeout") {
                using namespace std::chrono_literals;

                for (std::size_t i{0}; i < capacity; ++i) {
                    REQUIRE(sender.trySend(element));
                }

                auto task = asyncio::toThread([&] {
                    return sender.sendSyncEx(std::string{element}, 1s);
                });

                REQUIRE(co_await asyncio::sleep(10ms));
                REQUIRE(co_await receiver.receive() == element);
                REQUIRE(co_await task);
            }
        }

        SECTION("disconnected") {
            sender.close();

            const auto result = sender.sendSyncEx(std::string{element});
            REQUIRE_FALSE(result);
            REQUIRE(std::get<0>(result.error()) == element);
            REQUIRE(std::get<1>(result.error()) == asyncio::SendSyncError::DISCONNECTED);
        }

        SECTION("timeout") {
            using namespace std::chrono_literals;

            for (std::size_t i{0}; i < capacity; ++i) {
                REQUIRE(sender.trySend(element));
            }

            const auto result = sender.sendSyncEx(std::string{element}, 10ms);
            REQUIRE_FALSE(result);
            REQUIRE(std::get<0>(result.error()) == element);
            REQUIRE(std::get<1>(result.error()) == asyncio::SendSyncError::TIMEOUT);
        }
    }

    SECTION("send") {
        SECTION("success") {
            SECTION("no wait") {
                REQUIRE(co_await sender.send(element));
            }

            SECTION("wait") {
                for (std::size_t i{0}; i < capacity; ++i) {
                    REQUIRE(sender.trySend(element));
                }

                auto task = sender.send(element);
                REQUIRE(co_await receiver.receive() == element);
                REQUIRE(co_await task);
            }
        }

        SECTION("disconnected") {
            sender.close();
            REQUIRE_ERROR(co_await sender.send(element), asyncio::SendError::DISCONNECTED);
        }

        SECTION("cancelled") {
            using namespace std::chrono_literals;

            for (std::size_t i{0}; i < capacity; ++i) {
                REQUIRE(sender.trySend(element));
            }

            auto task = sender.send(element);
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, asyncio::SendError::CANCELLED);
        }
    }

    SECTION("send extended") {
        SECTION("success") {
            SECTION("no wait") {
                REQUIRE(co_await sender.sendEx(std::string{element}));
            }

            SECTION("wait") {
                for (std::size_t i{0}; i < capacity; ++i) {
                    REQUIRE(sender.trySend(element));
                }

                auto task = sender.sendEx(std::string{element});
                REQUIRE(co_await receiver.receive() == element);
                REQUIRE(co_await task);
            }
        }

        SECTION("disconnected") {
            sender.close();

            const auto result = sender.sendSyncEx(std::string{element});
            REQUIRE_FALSE(result);
            REQUIRE(std::get<0>(result.error()) == element);
            REQUIRE(std::get<1>(result.error()) == asyncio::SendSyncError::DISCONNECTED);
        }

        SECTION("cancelled") {
            using namespace std::chrono_literals;

            for (std::size_t i{0}; i < capacity; ++i) {
                REQUIRE(sender.trySend(element));
            }

            auto task = sender.sendEx(element);
            REQUIRE(task.cancel());

            const auto result = co_await task;
            REQUIRE_FALSE(result);
            REQUIRE(std::get<0>(result.error()) == element);
            REQUIRE(std::get<1>(result.error()) == asyncio::SendError::CANCELLED);
        }
    }

    SECTION("close") {
        sender.close();
        REQUIRE(sender.closed());
    }

    SECTION("size") {
        const auto size = GENERATE_REF(take(1, random(0uz, capacity)));

        for (std::size_t i{0}; i < size; ++i) {
            REQUIRE(sender.trySend(element));
        }

        REQUIRE(sender.size() == size);
    }

    SECTION("capacity") {
        REQUIRE(sender.capacity() == capacity);
    }

    SECTION("empty") {
        SECTION("empty") {
            REQUIRE(sender.empty());
        }

        SECTION("not empty") {
            REQUIRE(sender.trySend(element));
            REQUIRE_FALSE(sender.empty());
        }
    }

    SECTION("full") {
        SECTION("not full") {
            REQUIRE_FALSE(sender.full());
        }

        SECTION("full") {
            for (std::size_t i{0}; i < capacity; ++i) {
                REQUIRE(sender.trySend(element));
            }

            REQUIRE(sender.full());
        }
    }

    SECTION("closed") {
        SECTION("not closed") {
            REQUIRE_FALSE(sender.closed());
        }

        SECTION("closed") {
            sender.close();
            REQUIRE(sender.closed());
        }
    }
}

ASYNC_TEST_CASE("channel receiver", "[channel]") {
    const auto capacity = GENERATE(1uz, take(1, random(2uz, 1024uz)));
    const auto element = GENERATE(take(1, randomString(1, 1024)));

    auto [sender, receiver] = asyncio::channel<std::string>(capacity);

    SECTION("try receive") {
        SECTION("success") {
            REQUIRE(sender.trySend(element));

            SECTION("closed") {
                sender.close();
            }

            REQUIRE(receiver.tryReceive());
        }

        SECTION("disconnected") {
            sender.close();
            REQUIRE_ERROR(receiver.tryReceive(), asyncio::TryReceiveError::DISCONNECTED);
        }

        SECTION("empty") {
            REQUIRE_ERROR(receiver.tryReceive(), asyncio::TryReceiveError::EMPTY);
        }
    }

    SECTION("receive sync") {
        SECTION("success") {
            SECTION("no wait") {
                REQUIRE(sender.trySend(element));

                SECTION("closed") {
                    sender.close();
                }

                REQUIRE(receiver.receiveSync() == element);
            }

            SECTION("wait") {
                auto task = asyncio::toThread([&] {
                    return receiver.receiveSync();
                });

                REQUIRE(sender.trySend(element));

                SECTION("closed") {
                    sender.close();
                }

                REQUIRE(co_await task == element);
            }

            SECTION("wait with timeout") {
                using namespace std::chrono_literals;

                auto task = asyncio::toThread([&] {
                    return receiver.receiveSync(10ms);
                });

                REQUIRE(sender.trySend(element));

                SECTION("closed") {
                    sender.close();
                }

                REQUIRE(co_await task == element);
            }
        }

        SECTION("disconnected") {
            SECTION("after close") {
                sender.close();
                REQUIRE_ERROR(receiver.receiveSync(), asyncio::ReceiveSyncError::DISCONNECTED);
            }

            SECTION("before close") {
                auto task = asyncio::toThread([&] {
                    return receiver.receiveSync();
                });
                sender.close();
                REQUIRE_ERROR(co_await task, asyncio::ReceiveSyncError::DISCONNECTED);
            }
        }

        SECTION("timeout") {
            using namespace std::chrono_literals;
            REQUIRE_ERROR(receiver.receiveSync(10ms), asyncio::ReceiveSyncError::TIMEOUT);
        }
    }

    SECTION("receive") {
        SECTION("success") {
            SECTION("no wait") {
                REQUIRE(sender.trySend(element));

                SECTION("closed") {
                    sender.close();
                }

                REQUIRE(co_await receiver.receive() == element);
            }

            SECTION("wait") {
                auto task = receiver.receive();
                REQUIRE(sender.trySend(element));

                SECTION("closed") {
                    sender.close();
                }

                REQUIRE(co_await task == element);
            }
        }

        SECTION("disconnected") {
            SECTION("after close") {
                sender.close();
                REQUIRE_ERROR(co_await receiver.receive(), asyncio::ReceiveError::DISCONNECTED);
            }

            SECTION("before close") {
                auto task = receiver.receive();
                sender.close();
                REQUIRE_ERROR(co_await task, asyncio::ReceiveError::DISCONNECTED);
            }
        }

        SECTION("cancelled") {
            using namespace std::chrono_literals;
            auto task = receiver.receive();
            REQUIRE(task.cancel());
            REQUIRE_ERROR(co_await task, asyncio::ReceiveError::CANCELLED);
        }
    }

    SECTION("size") {
        const auto size = GENERATE_REF(take(1, random(0uz, capacity)));

        for (std::size_t i{0}; i < size; ++i) {
            REQUIRE(sender.trySend(element));
        }

        REQUIRE(receiver.size() == size);
    }

    SECTION("capacity") {
        REQUIRE(receiver.capacity() == capacity);
    }

    SECTION("empty") {
        SECTION("empty") {
            REQUIRE(receiver.empty());
        }

        SECTION("not empty") {
            REQUIRE(sender.trySend(element));
            REQUIRE_FALSE(receiver.empty());
        }
    }

    SECTION("full") {
        SECTION("not full") {
            REQUIRE_FALSE(receiver.full());
        }

        SECTION("full") {
            for (std::size_t i{0}; i < capacity; ++i) {
                REQUIRE(sender.trySend(element));
            }

            REQUIRE(receiver.full());
        }
    }

    SECTION("closed") {
        SECTION("not closed") {
            REQUIRE_FALSE(receiver.closed());
        }

        SECTION("closed") {
            sender.close();
            REQUIRE(receiver.closed());
        }
    }
}

ASYNC_TEST_CASE("channel receiver dropped", "[channel]") {
    const auto capacity = GENERATE(take(1, random(1uz, 1024uz)));
    const auto element = GENERATE(take(1, randomString(1, 1024)));

    auto [sender, receiver] = asyncio::channel<std::string>(capacity);

    auto task = asyncio::toThread(
        [receiver = std::move(receiver)] mutable {
            return receiver.receiveSync();
        }
    );

    REQUIRE(sender.trySend(element));
    REQUIRE(co_await task == element);
    REQUIRE(sender.closed());
}

ASYNC_TEST_CASE("channel sender dropped", "[channel]") {
    const auto capacity = GENERATE(take(1, random(1uz, 1024uz)));
    const auto element = GENERATE(take(1, randomString(1, 1024)));

    auto [sender, receiver] = asyncio::channel<std::string>(capacity);

    auto task = asyncio::toThread(
        [&, sender = std::move(sender)] mutable {
            return sender.trySend(element);
        }
    );

    REQUIRE(co_await receiver.receive() == element);
    REQUIRE(co_await task);
    REQUIRE(receiver.closed());
}

ASYNC_TEST_CASE("channel concurrency testing", "[channel]") {
    const auto capacity = GENERATE(take(5, random(1uz, 1024uz)));
    const auto element = GENERATE(take(1, randomString(1, 1024)));
    const auto times = GENERATE(take(5, random(1, 102400)));

    auto [sender, receiver] = asyncio::channel<std::string>(capacity);

    std::atomic<int> counter;

    const auto produce = [&]() -> asyncio::task::Task<void, std::error_code> {
        for (int i{0}; i < times; ++i) {
            CO_EXPECT(co_await sender.send(element));
        }

        co_return {};
    };

    const auto produceSync = [&]() -> std::expected<void, std::error_code> {
        for (int i{0}; i < times; ++i) {
            EXPECT(sender.sendSync(element));
        }

        return {};
    };

    const auto consume = [&]() -> asyncio::task::Task<void, std::error_code> {
        while (true) {
            const auto result = co_await receiver.receive();

            if (!result) {
                if (result.error() == asyncio::ReceiveError::DISCONNECTED)
                    co_return {};

                co_return std::unexpected{result.error()};
            }

            if (*result != element)
                co_return std::unexpected{make_error_code(std::errc::bad_message)};

            ++counter;
        }
    };

    const auto consumeSync = [&]() -> std::expected<void, std::error_code> {
        while (true) {
            const auto result = receiver.receiveSync();

            if (!result) {
                if (result.error() == asyncio::ReceiveSyncError::DISCONNECTED)
                    return {};

                return std::unexpected{result.error()};
            }

            if (*result != element)
                return std::unexpected{make_error_code(std::errc::bad_message)};

            ++counter;
        }
    };

    std::array producers{asyncio::task::spawn(produce), asyncio::task::spawn(produce)};
    std::array syncProducers{asyncio::toThread(produceSync), asyncio::toThread(produceSync)};
    std::array consumers{asyncio::task::spawn(consume), asyncio::task::spawn(consume)};
    std::array syncConsumers{asyncio::toThread(consumeSync), asyncio::toThread(consumeSync)};

    for (auto &task: producers) {
        REQUIRE(co_await task);
    }

    for (auto &task: syncProducers) {
        REQUIRE(co_await task);
    }

    sender.close();

    for (auto &task: consumers) {
        REQUIRE(co_await task);
    }

    for (auto &task: syncConsumers) {
        REQUIRE(co_await task);
    }

    REQUIRE(counter == times * 4);
}

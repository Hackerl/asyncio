#ifndef ASYNCIO_CHANNEL_H
#define ASYNCIO_CHANNEL_H

#include "task.h"
#include <chrono>
#include <zero/atomic/circular_buffer.h>

namespace asyncio {
    static constexpr auto SENDER = 0;
    static constexpr auto RECEIVER = 1;

    template<typename T>
    struct ChannelCore {
        explicit ChannelCore(std::shared_ptr<EventLoop> e, const std::size_t capacity)
            : eventLoop{std::move(e)}, buffer{capacity} {
        }

        std::mutex mutex;
        std::atomic<bool> closed;
        std::shared_ptr<EventLoop> eventLoop;
        zero::atomic::CircularBuffer<T> buffer;
        std::array<std::list<std::shared_ptr<Promise<void, std::error_code>>>, 2> pending;
        std::array<std::atomic<std::size_t>, 2> counters;

        void trigger(const std::size_t index) {
            std::lock_guard guard{mutex};

            if (pending[index].empty())
                return;

            for (const auto &promise: std::exchange(pending[index], {})) {
                if (promise->isFulfilled())
                    continue;

                promise->resolve();
            }
        }

        void close() {
            {
                std::lock_guard guard{mutex};

                if (closed)
                    return;

                closed = true;
            }

            trigger(SENDER);
            trigger(RECEIVER);
        }
    };

    DEFINE_ERROR_CODE_EX(
        TrySendError,
        "asyncio::Sender::trySend",
        DISCONNECTED, "sending on a disconnected channel", DEFAULT_ERROR_CONDITION,
        FULL, "sending on a full channel", std::errc::operation_would_block
    )

    DEFINE_ERROR_CODE_EX(
        SendSyncError,
        "asyncio::Sender::sendSync",
        DISCONNECTED, "sending on a disconnected channel", DEFAULT_ERROR_CONDITION,
        TIMEOUT, "timed out waiting on send operation", std::errc::timed_out
    )

    DEFINE_ERROR_CODE_EX(
        SendError,
        "asyncio::Sender::send",
        DISCONNECTED, "sending on a disconnected channel", DEFAULT_ERROR_CONDITION,
        CANCELLED, "send operation has been cancelled", std::errc::operation_canceled
    )

    template<typename T>
    class Sender {
    public:
        explicit Sender(std::shared_ptr<ChannelCore<T>> core) : mCore{std::move(core)} {
            ++mCore->counters[SENDER];
        }

        Sender(const Sender &rhs) : mCore{rhs.mCore} {
            ++mCore->counters[SENDER];
        }

        Sender(Sender &&rhs) = default;

        Sender &operator=(const Sender &rhs) {
            mCore = rhs.mCore;
            ++mCore->counters[SENDER];
            return *this;
        }

        Sender &operator=(Sender &&rhs) noexcept = default;

        ~Sender() {
            if (!mCore)
                return;

            if (--mCore->counters[SENDER] > 0)
                return;

            mCore->close();
        }

        template<typename U = T>
        std::expected<void, TrySendError> trySend(U &&element) {
            if (mCore->closed)
                return std::unexpected{TrySendError::DISCONNECTED};

            const auto index = mCore->buffer.reserve();

            if (!index)
                return std::unexpected{TrySendError::FULL};

            mCore->buffer[*index] = std::forward<U>(element);
            mCore->buffer.commit(*index);
            mCore->trigger(RECEIVER);

            return {};
        }

        template<typename U = T>
        std::expected<void, SendSyncError>
        sendSync(U &&element, const std::optional<std::chrono::milliseconds> timeout = std::nullopt) {
            if (mCore->closed)
                return std::unexpected{SendSyncError::DISCONNECTED};

            while (true) {
                const auto index = mCore->buffer.reserve();

                if (index) {
                    mCore->buffer[*index] = std::forward<U>(element);
                    mCore->buffer.commit(*index);
                    mCore->trigger(RECEIVER);
                    return {};
                }

                mCore->mutex.lock();

                if (mCore->closed) {
                    mCore->mutex.unlock();
                    return std::unexpected{SendSyncError::DISCONNECTED};
                }

                if (!mCore->buffer.full()) {
                    mCore->mutex.unlock();
                    continue;
                }

                const auto promise = std::make_shared<Promise<void, std::error_code>>(mCore->eventLoop);
                const auto future = promise->getFuture();

                mCore->pending[SENDER].push_back(promise);
                mCore->mutex.unlock();

                if (const auto result = future.wait(timeout); !result) {
                    assert(result.error() == std::errc::timed_out);
                    std::lock_guard guard{mCore->mutex};
                    mCore->pending[SENDER].remove(promise);
                    return std::unexpected{SendSyncError::TIMEOUT};
                }
            }
        }

        task::Task<void, SendError> send(T element) {
            if (mCore->closed)
                co_return std::unexpected{SendError::DISCONNECTED};

            while (true) {
                const auto index = mCore->buffer.reserve();

                if (index) {
                    mCore->buffer[*index] = std::move(element);
                    mCore->buffer.commit(*index);
                    mCore->trigger(RECEIVER);
                    co_return {};
                }

                mCore->mutex.lock();

                if (mCore->closed) {
                    mCore->mutex.unlock();
                    co_return std::unexpected{SendError::DISCONNECTED};
                }

                if (!mCore->buffer.full()) {
                    mCore->mutex.unlock();
                    continue;
                }

                const auto promise = std::make_shared<Promise<void, std::error_code>>(mCore->eventLoop);

                mCore->pending[SENDER].push_back(promise);
                mCore->mutex.unlock();

                if (const auto result = co_await task::Cancellable{
                    promise->getFuture(),
                    [=]() -> std::expected<void, std::error_code> {
                        if (promise->isFulfilled())
                            return std::unexpected{task::Error::WILL_BE_DONE};

                        promise->reject(task::Error::CANCELLED);
                        return {};
                    }
                }; !result) {
                    assert(result.error() == std::errc::operation_canceled);
                    std::lock_guard guard{mCore->mutex};
                    mCore->pending[SENDER].remove(promise);
                    co_return std::unexpected{SendError::CANCELLED};
                }
            }
        }

        void close() {
            mCore->close();
        }

        [[nodiscard]] std::size_t size() const {
            return mCore->buffer.size();
        }

        [[nodiscard]] std::size_t capacity() const {
            return mCore->buffer.capacity();
        }

        [[nodiscard]] bool empty() const {
            return mCore->buffer.empty();
        }

        [[nodiscard]] bool full() const {
            return mCore->buffer.full();
        }

        [[nodiscard]] bool closed() const {
            return mCore->closed;
        }

    private:
        std::shared_ptr<ChannelCore<T>> mCore;
    };

    DEFINE_ERROR_CODE_EX(
        TryReceiveError,
        "asyncio::Sender::tryReceive",
        DISCONNECTED, "receiving on an empty and disconnected channel", DEFAULT_ERROR_CONDITION,
        EMPTY, "receiving on an empty channel", std::errc::operation_would_block
    )

    DEFINE_ERROR_CODE_EX(
        ReceiveSyncError,
        "asyncio::Sender::receiveSync",
        DISCONNECTED, "channel is empty and disconnected", DEFAULT_ERROR_CONDITION,
        TIMEOUT, "timed out waiting on receive operation", std::errc::timed_out
    )

    DEFINE_ERROR_CODE_EX(
        ReceiveError,
        "asyncio::Sender::receive",
        DISCONNECTED, "channel is empty and disconnected", DEFAULT_ERROR_CONDITION,
        CANCELLED, "receive operation has been cancelled", std::errc::operation_canceled
    )

    template<typename T>
    class Receiver {
    public:
        explicit Receiver(std::shared_ptr<ChannelCore<T>> core) : mCore{std::move(core)} {
            ++mCore->counters[RECEIVER];
        }

        Receiver(const Receiver &rhs) : mCore{rhs.mCore} {
            ++mCore->counters[RECEIVER];
        }

        Receiver(Receiver &&rhs) = default;

        Receiver &operator=(const Receiver &rhs) {
            mCore = rhs.mCore;
            ++mCore->counters[RECEIVER];
            return *this;
        }

        Receiver &operator=(Receiver &&rhs) noexcept = default;

        ~Receiver() {
            if (!mCore)
                return;

            if (--mCore->counters[RECEIVER] > 0)
                return;

            mCore->close();
        }

        std::expected<T, TryReceiveError> tryReceive() {
            const auto index = mCore->buffer.acquire();

            if (!index)
                return std::unexpected{mCore->closed ? TryReceiveError::DISCONNECTED : TryReceiveError::EMPTY};

            auto element = std::move(mCore->buffer[*index]);

            mCore->buffer.release(*index);
            mCore->trigger(SENDER);

            return element;
        }

        std::expected<T, ReceiveSyncError>
        receiveSync(const std::optional<std::chrono::milliseconds> timeout = std::nullopt) {
            while (true) {
                const auto index = mCore->buffer.acquire();

                if (index) {
                    auto element = std::move(mCore->buffer[*index]);
                    mCore->buffer.release(*index);
                    mCore->trigger(SENDER);
                    return element;
                }

                mCore->mutex.lock();

                if (!mCore->buffer.empty()) {
                    mCore->mutex.unlock();
                    continue;
                }

                if (mCore->closed) {
                    mCore->mutex.unlock();
                    return std::unexpected{ReceiveSyncError::DISCONNECTED};
                }

                const auto promise = std::make_shared<Promise<void, std::error_code>>(mCore->eventLoop);
                const auto future = promise->getFuture();

                mCore->pending[RECEIVER].push_back(promise);
                mCore->mutex.unlock();

                if (const auto result = future.wait(timeout); !result) {
                    assert(result.error() == std::errc::timed_out);
                    std::lock_guard guard{mCore->mutex};
                    mCore->pending[RECEIVER].remove(promise);
                    return std::unexpected{ReceiveSyncError::TIMEOUT};
                }
            }
        }

        task::Task<T, ReceiveError> receive() {
            while (true) {
                const auto index = mCore->buffer.acquire();

                if (index) {
                    auto element = std::move(mCore->buffer[*index]);
                    mCore->buffer.release(*index);
                    mCore->trigger(SENDER);
                    co_return element;
                }

                mCore->mutex.lock();

                if (!mCore->buffer.empty()) {
                    mCore->mutex.unlock();
                    continue;
                }

                if (mCore->closed) {
                    mCore->mutex.unlock();
                    co_return std::unexpected{ReceiveError::DISCONNECTED};
                }

                const auto promise = std::make_shared<Promise<void, std::error_code>>(mCore->eventLoop);

                mCore->pending[RECEIVER].push_back(promise);
                mCore->mutex.unlock();

                if (const auto result = co_await task::Cancellable{
                    promise->getFuture(),
                    [=]() -> std::expected<void, std::error_code> {
                        if (promise->isFulfilled())
                            return std::unexpected{task::Error::WILL_BE_DONE};

                        promise->reject(task::Error::CANCELLED);
                        return {};
                    }
                }; !result) {
                    assert(result.error() == std::errc::operation_canceled);
                    std::lock_guard guard{mCore->mutex};
                    mCore->pending[RECEIVER].remove(promise);
                    co_return std::unexpected{ReceiveError::CANCELLED};
                }
            }
        }

        [[nodiscard]] std::size_t size() const {
            return mCore->buffer.size();
        }

        [[nodiscard]] std::size_t capacity() const {
            return mCore->buffer.capacity();
        }

        [[nodiscard]] bool empty() const {
            return mCore->buffer.empty();
        }

        [[nodiscard]] bool full() const {
            return mCore->buffer.full();
        }

        [[nodiscard]] bool closed() const {
            return mCore->closed;
        }

    private:
        std::shared_ptr<ChannelCore<T>> mCore;
    };

    DEFINE_ERROR_CONDITION_EX(
        ChannelError,
        "asyncio::channel",
        DISCONNECTED,
        "channel disconnected",
        [](const std::error_code &ec) {
            return ec == make_error_code(TrySendError::DISCONNECTED) ||
                ec == make_error_code(SendSyncError::DISCONNECTED) ||
                ec == make_error_code(SendError::DISCONNECTED) ||
                ec == make_error_code(TryReceiveError::DISCONNECTED) ||
                ec == make_error_code(ReceiveSyncError::DISCONNECTED) ||
                ec == make_error_code(ReceiveError::DISCONNECTED);
        }
    )

    template<typename T>
    using Channel = std::pair<Sender<T>, Receiver<T>>;

    template<typename T>
    Channel<T> channel(std::shared_ptr<EventLoop> eventLoop, const std::size_t capacity) {
        const auto core = std::make_shared<ChannelCore<T>>(std::move(eventLoop), capacity);
        return {Sender<T>{core}, Receiver<T>{core}};
    }

    template<typename T>
    Channel<T> channel(const std::size_t capacity) {
        return channel<T>(getEventLoop(), capacity);
    }
}

DECLARE_ERROR_CODES(
    asyncio::TrySendError,
    asyncio::SendSyncError,
    asyncio::SendError,
    asyncio::TryReceiveError,
    asyncio::ReceiveSyncError,
    asyncio::ReceiveError
)

DECLARE_ERROR_CONDITION(asyncio::ChannelError)

#endif //ASYNCIO_CHANNEL_H

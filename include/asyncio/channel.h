#ifndef ASYNCIO_CHANNEL_H
#define ASYNCIO_CHANNEL_H

#include "task.h"
#include <chrono>
#include <zero/atomic/circular_buffer.h>

namespace asyncio {
    template<typename T>
    struct ChannelCore {
        struct Context {
            std::list<std::shared_ptr<Promise<void, std::error_code>>> pending;
            std::atomic<std::size_t> counter;
        };

        explicit ChannelCore(std::shared_ptr<EventLoop> e, const std::size_t capacity)
            : eventLoop{std::move(e)}, buffer{capacity + 1} {
        }

        std::mutex mutex;
        std::atomic<bool> closed;
        std::shared_ptr<EventLoop> eventLoop;
        zero::atomic::CircularBuffer<T> buffer;
        Context sender;
        Context receiver;

        void notifySender() {
            const std::lock_guard guard{mutex};

            auto &pending = sender.pending;

            if (pending.empty())
                return;

            for (const auto &promise: std::exchange(pending, {})) {
                if (promise->isFulfilled())
                    continue;

                promise->resolve();
            }
        }

        void notifyReceiver() {
            const std::lock_guard guard{mutex};

            auto &pending = receiver.pending;

            if (pending.empty())
                return;

            for (const auto &promise: std::exchange(pending, {})) {
                if (promise->isFulfilled())
                    continue;

                promise->resolve();
            }
        }

        void close() {
            {
                const std::lock_guard guard{mutex};

                if (closed)
                    return;

                closed = true;
            }

            notifySender();
            notifyReceiver();
        }
    };

    Z_DEFINE_ERROR_CODE_EX(
        TrySendError,
        "asyncio::Sender::trySend",
        Disconnected, "Sending on a disconnected channel", Z_DEFAULT_ERROR_CONDITION,
        Full, "Sending on a full channel", std::errc::operation_would_block
    )

    Z_DEFINE_ERROR_CODE_EX(
        SendSyncError,
        "asyncio::Sender::sendSync",
        Disconnected, "Sending on a disconnected channel", Z_DEFAULT_ERROR_CONDITION,
        Timeout, "Send operation timed out", std::errc::timed_out
    )

    Z_DEFINE_ERROR_CODE_EX(
        SendError,
        "asyncio::Sender::send",
        Disconnected, "Sending on a disconnected channel", Z_DEFAULT_ERROR_CONDITION,
        Cancelled, "Send operation was cancelled", std::errc::operation_canceled
    )

    template<typename T>
    class Sender {
    public:
        explicit Sender(std::shared_ptr<ChannelCore<T>> core) : mCore{std::move(core)} {
            ++mCore->sender.counter;
        }

        Sender(const Sender &rhs) : mCore{rhs.mCore} {
            ++mCore->sender.counter;
        }

        Sender(Sender &&rhs) = default;

        Sender &operator=(const Sender &rhs) {
            mCore = rhs.mCore;
            ++mCore->sender.counter;
            return *this;
        }

        Sender &operator=(Sender &&rhs) noexcept = default;

        ~Sender() {
            if (!mCore)
                return;

            if (--mCore->sender.counter > 0)
                return;

            mCore->close();
        }

        template<typename U = T>
        std::expected<void, TrySendError> trySend(U &&element) {
            if (mCore->closed)
                return std::unexpected{TrySendError::Disconnected};

            const auto index = mCore->buffer.reserve();

            if (!index)
                return std::unexpected{TrySendError::Full};

            mCore->buffer[*index] = std::forward<U>(element);
            mCore->buffer.commit(*index);
            mCore->notifyReceiver();

            return {};
        }

        std::expected<void, std::pair<T, TrySendError>> trySendEx(T &&element) {
            if (mCore->closed)
                return std::unexpected{std::pair{std::move(element), TrySendError::Disconnected}};

            const auto index = mCore->buffer.reserve();

            if (!index)
                return std::unexpected{std::pair{std::move(element), TrySendError::Full}};

            mCore->buffer[*index] = std::move(element);
            mCore->buffer.commit(*index);
            mCore->notifyReceiver();

            return {};
        }

        template<typename U = T>
        std::expected<void, SendSyncError>
        sendSync(U &&element, const std::optional<std::chrono::milliseconds> timeout = std::nullopt) {
            if (mCore->closed)
                return std::unexpected{SendSyncError::Disconnected};

            while (true) {
                const auto index = mCore->buffer.reserve();

                if (index) {
                    mCore->buffer[*index] = std::forward<U>(element);
                    mCore->buffer.commit(*index);
                    mCore->notifyReceiver();
                    return {};
                }

                mCore->mutex.lock();

                if (mCore->closed) {
                    mCore->mutex.unlock();
                    return std::unexpected{SendSyncError::Disconnected};
                }

                if (!mCore->buffer.full()) {
                    mCore->mutex.unlock();
                    continue;
                }

                const auto promise = std::make_shared<Promise<void, std::error_code>>();

                mCore->sender.pending.push_back(promise);
                mCore->mutex.unlock();

                if (const auto result = promise->getFuture().wait(timeout); !result) {
                    assert(result.error() == std::errc::timed_out);
                    const std::lock_guard guard{mCore->mutex};
                    mCore->sender.pending.remove(promise);
                    return std::unexpected{SendSyncError::Timeout};
                }
            }
        }

        std::expected<void, std::pair<T, SendSyncError>>
        sendSyncEx(T &&element, const std::optional<std::chrono::milliseconds> timeout = std::nullopt) {
            if (mCore->closed)
                return std::unexpected{std::pair{std::move(element), SendSyncError::Disconnected}};

            while (true) {
                const auto index = mCore->buffer.reserve();

                if (index) {
                    mCore->buffer[*index] = std::move(element);
                    mCore->buffer.commit(*index);
                    mCore->notifyReceiver();
                    return {};
                }

                mCore->mutex.lock();

                if (mCore->closed) {
                    mCore->mutex.unlock();
                    return std::unexpected{std::pair{std::move(element), SendSyncError::Disconnected}};
                }

                if (!mCore->buffer.full()) {
                    mCore->mutex.unlock();
                    continue;
                }

                const auto promise = std::make_shared<Promise<void, std::error_code>>();

                mCore->sender.pending.push_back(promise);
                mCore->mutex.unlock();

                if (const auto result = promise->getFuture().wait(timeout); !result) {
                    assert(result.error() == std::errc::timed_out);
                    const std::lock_guard guard{mCore->mutex};
                    mCore->sender.pending.remove(promise);
                    return std::unexpected{std::pair{std::move(element), SendSyncError::Timeout}};
                }
            }
        }

        task::Task<void, SendError> send(T element) {
            if (mCore->closed)
                co_return std::unexpected{SendError::Disconnected};

            while (true) {
                const auto index = mCore->buffer.reserve();

                if (index) {
                    mCore->buffer[*index] = std::move(element);
                    mCore->buffer.commit(*index);
                    mCore->notifyReceiver();
                    co_return {};
                }

                mCore->mutex.lock();

                if (mCore->closed) {
                    mCore->mutex.unlock();
                    co_return std::unexpected{SendError::Disconnected};
                }

                if (!mCore->buffer.full()) {
                    mCore->mutex.unlock();
                    continue;
                }

                const auto promise = std::make_shared<Promise<void, std::error_code>>();

                mCore->sender.pending.push_back(promise);
                mCore->mutex.unlock();

                if (const auto result = co_await task::Cancellable{
                    promise->getFuture(),
                    [=]() -> std::expected<void, std::error_code> {
                        if (promise->isFulfilled())
                            return std::unexpected{task::Error::CancellationTooLate};

                        promise->reject(task::Error::Cancelled);
                        return {};
                    }
                }; !result) {
                    assert(result.error() == std::errc::operation_canceled);
                    const std::lock_guard guard{mCore->mutex};
                    mCore->sender.pending.remove(promise);
                    co_return std::unexpected{SendError::Cancelled};
                }
            }
        }

        task::Task<void, std::pair<T, SendError>> sendEx(T element) {
            if (mCore->closed)
                co_return std::unexpected{std::pair{std::move(element), SendError::Disconnected}};

            while (true) {
                const auto index = mCore->buffer.reserve();

                if (index) {
                    mCore->buffer[*index] = std::move(element);
                    mCore->buffer.commit(*index);
                    mCore->notifyReceiver();
                    co_return {};
                }

                mCore->mutex.lock();

                if (mCore->closed) {
                    mCore->mutex.unlock();
                    co_return std::unexpected{std::pair{std::move(element), SendError::Disconnected}};
                }

                if (!mCore->buffer.full()) {
                    mCore->mutex.unlock();
                    continue;
                }

                const auto promise = std::make_shared<Promise<void, std::error_code>>();

                mCore->sender.pending.push_back(promise);
                mCore->mutex.unlock();

                if (const auto result = co_await task::Cancellable{
                    promise->getFuture(),
                    [=]() -> std::expected<void, std::error_code> {
                        if (promise->isFulfilled())
                            return std::unexpected{task::Error::CancellationTooLate};

                        promise->reject(task::Error::Cancelled);
                        return {};
                    }
                }; !result) {
                    assert(result.error() == std::errc::operation_canceled);
                    const std::lock_guard guard{mCore->mutex};
                    mCore->sender.pending.remove(promise);
                    co_return std::unexpected{std::pair{std::move(element), SendError::Cancelled}};
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
            return mCore->buffer.capacity() - 1;
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

    Z_DEFINE_ERROR_CODE_EX(
        TryReceiveError,
        "asyncio::Receiver::tryReceive",
        Disconnected, "Receiving on an empty and disconnected channel", Z_DEFAULT_ERROR_CONDITION,
        Empty, "Receiving on an empty channel", std::errc::operation_would_block
    )

    Z_DEFINE_ERROR_CODE_EX(
        ReceiveSyncError,
        "asyncio::Receiver::receiveSync",
        Disconnected, "Receiving on an empty and disconnected channel", Z_DEFAULT_ERROR_CONDITION,
        Timeout, "Receive operation timed out", std::errc::timed_out
    )

    Z_DEFINE_ERROR_CODE_EX(
        ReceiveError,
        "asyncio::Receiver::receive",
        Disconnected, "Receiving on an empty and disconnected channel", Z_DEFAULT_ERROR_CONDITION,
        Cancelled, "Receive operation was cancelled", std::errc::operation_canceled
    )

    template<typename T>
    class Receiver {
    public:
        explicit Receiver(std::shared_ptr<ChannelCore<T>> core) : mCore{std::move(core)} {
            ++mCore->receiver.counter;
        }

        Receiver(const Receiver &rhs) : mCore{rhs.mCore} {
            ++mCore->receiver.counter;
        }

        Receiver(Receiver &&rhs) = default;

        Receiver &operator=(const Receiver &rhs) {
            mCore = rhs.mCore;
            ++mCore->receiver.counter;
            return *this;
        }

        Receiver &operator=(Receiver &&rhs) noexcept = default;

        ~Receiver() {
            if (!mCore)
                return;

            if (--mCore->receiver.counter > 0)
                return;

            mCore->close();
        }

        std::expected<T, TryReceiveError> tryReceive() {
            const auto index = mCore->buffer.acquire();

            if (!index)
                return std::unexpected{mCore->closed ? TryReceiveError::Disconnected : TryReceiveError::Empty};

            auto element = std::move(mCore->buffer[*index]);

            mCore->buffer.release(*index);
            mCore->notifySender();

            return element;
        }

        std::expected<T, ReceiveSyncError>
        receiveSync(const std::optional<std::chrono::milliseconds> timeout = std::nullopt) {
            while (true) {
                const auto index = mCore->buffer.acquire();

                if (index) {
                    auto element = std::move(mCore->buffer[*index]);
                    mCore->buffer.release(*index);
                    mCore->notifySender();
                    return element;
                }

                mCore->mutex.lock();

                if (!mCore->buffer.empty()) {
                    mCore->mutex.unlock();
                    continue;
                }

                if (mCore->closed) {
                    mCore->mutex.unlock();
                    return std::unexpected{ReceiveSyncError::Disconnected};
                }

                const auto promise = std::make_shared<Promise<void, std::error_code>>();

                mCore->receiver.pending.push_back(promise);
                mCore->mutex.unlock();

                if (const auto result = promise->getFuture().wait(timeout); !result) {
                    assert(result.error() == std::errc::timed_out);
                    const std::lock_guard guard{mCore->mutex};
                    mCore->receiver.pending.remove(promise);
                    return std::unexpected{ReceiveSyncError::Timeout};
                }
            }
        }

        task::Task<T, ReceiveError> receive() {
            while (true) {
                const auto index = mCore->buffer.acquire();

                if (index) {
                    auto element = std::move(mCore->buffer[*index]);
                    mCore->buffer.release(*index);
                    mCore->notifySender();
                    co_return element;
                }

                mCore->mutex.lock();

                if (!mCore->buffer.empty()) {
                    mCore->mutex.unlock();
                    continue;
                }

                if (mCore->closed) {
                    mCore->mutex.unlock();
                    co_return std::unexpected{ReceiveError::Disconnected};
                }

                const auto promise = std::make_shared<Promise<void, std::error_code>>();

                mCore->receiver.pending.push_back(promise);
                mCore->mutex.unlock();

                if (const auto result = co_await task::Cancellable{
                    promise->getFuture(),
                    [=]() -> std::expected<void, std::error_code> {
                        if (promise->isFulfilled())
                            return std::unexpected{task::Error::CancellationTooLate};

                        promise->reject(task::Error::Cancelled);
                        return {};
                    }
                }; !result) {
                    assert(result.error() == std::errc::operation_canceled);
                    const std::lock_guard guard{mCore->mutex};
                    mCore->receiver.pending.remove(promise);
                    co_return std::unexpected{ReceiveError::Cancelled};
                }
            }
        }

        [[nodiscard]] std::size_t size() const {
            return mCore->buffer.size();
        }

        [[nodiscard]] std::size_t capacity() const {
            return mCore->buffer.capacity() - 1;
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

    Z_DEFINE_ERROR_CONDITION_EX(
        ChannelError,
        "asyncio::channel",
        Disconnected,
        "Channel disconnected",
        [](const std::error_code &ec) {
            return ec == make_error_code(TrySendError::Disconnected) ||
                ec == make_error_code(SendSyncError::Disconnected) ||
                ec == make_error_code(SendError::Disconnected) ||
                ec == make_error_code(TryReceiveError::Disconnected) ||
                ec == make_error_code(ReceiveSyncError::Disconnected) ||
                ec == make_error_code(ReceiveError::Disconnected);
        }
    )

    template<typename T>
    using Channel = std::pair<Sender<T>, Receiver<T>>;

    template<typename T>
    Channel<T> channel(std::shared_ptr<EventLoop> eventLoop, const std::size_t capacity = 1) {
        const auto core = std::make_shared<ChannelCore<T>>(std::move(eventLoop), capacity);
        return {Sender<T>{core}, Receiver<T>{core}};
    }

    template<typename T>
    Channel<T> channel(const std::size_t capacity = 1) {
        return channel<T>(getEventLoop(), capacity);
    }
}

Z_DECLARE_ERROR_CODES(
    asyncio::TrySendError,
    asyncio::SendSyncError,
    asyncio::SendError,
    asyncio::TryReceiveError,
    asyncio::ReceiveSyncError,
    asyncio::ReceiveError
)

Z_DECLARE_ERROR_CONDITION(asyncio::ChannelError)

#endif //ASYNCIO_CHANNEL_H

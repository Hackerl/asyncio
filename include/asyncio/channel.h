#ifndef ASYNCIO_CHANNEL_H
#define ASYNCIO_CHANNEL_H

#include "promise.h"
#include <chrono>
#include <zero/async/coroutine.h>
#include <zero/atomic/event.h>
#include <zero/atomic/circular_buffer.h>

namespace asyncio {
    static constexpr auto SENDER = 0;
    static constexpr auto RECEIVER = 1;

    template<typename T>
    struct ChannelCore {
        explicit ChannelCore(std::shared_ptr<EventLoop> e, const std::size_t capacity)
            : eventLoop(std::move(e)), buffer(capacity) {
        }

        std::mutex mutex;
        std::atomic<bool> closed;
        std::shared_ptr<EventLoop> eventLoop;
        zero::atomic::CircularBuffer<T> buffer;
        std::array<std::list<std::shared_ptr<Promise<void, std::error_code>>>, 2> pending;
        std::array<std::atomic<std::size_t>, 2> counters;

        void trigger(const std::size_t index) {
            std::lock_guard guard(mutex);

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
                std::lock_guard guard(mutex);

                if (closed)
                    return;

                closed = true;
            }

            trigger(SENDER);
            trigger(RECEIVER);
        }
    };

    enum class TrySendError {
        DISCONNECTED = 1,
        FULL
    };

    class TrySendErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
        [[nodiscard]] std::error_condition default_error_condition(int value) const noexcept override;
    };

    std::error_code make_error_code(TrySendError e);

    enum class SendSyncError {
        DISCONNECTED = 1,
        TIMEOUT
    };

    class SendSyncErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
        [[nodiscard]] std::error_condition default_error_condition(int value) const noexcept override;
    };

    std::error_code make_error_code(SendSyncError e);

    enum class SendError {
        DISCONNECTED = 1,
        CANCELLED
    };

    class SendErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
        [[nodiscard]] std::error_condition default_error_condition(int value) const noexcept override;
    };

    std::error_code make_error_code(SendError e);

    template<typename T>
    class Sender {
    public:
        explicit Sender(std::shared_ptr<ChannelCore<T>> core) : mCore(std::move(core)) {
            ++mCore->counters[SENDER];
        }

        Sender(const Sender &rhs) : mCore(rhs.mCore) {
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

        template<typename U>
        tl::expected<void, TrySendError> trySend(U &&element) {
            if (mCore->closed)
                return tl::unexpected(TrySendError::DISCONNECTED);

            const auto index = mCore->buffer.reserve();

            if (!index)
                return tl::unexpected(TrySendError::FULL);

            mCore->buffer[*index] = std::forward<U>(element);
            mCore->buffer.commit(*index);
            mCore->trigger(RECEIVER);

            return {};
        }

        template<typename U>
        tl::expected<void, SendSyncError>
        sendSync(U &&element, const std::optional<std::chrono::milliseconds> timeout = std::nullopt) {
            if (mCore->closed)
                return tl::unexpected(SendSyncError::DISCONNECTED);

            tl::expected<void, SendSyncError> result;

            while (true) {
                const auto index = mCore->buffer.reserve();

                if (!index) {
                    mCore->mutex.lock();

                    if (mCore->closed) {
                        mCore->mutex.unlock();
                        result = tl::unexpected(SendSyncError::DISCONNECTED);
                        break;
                    }

                    if (!mCore->buffer.full()) {
                        mCore->mutex.unlock();
                        continue;
                    }

                    const auto event = std::make_shared<zero::atomic::Event>();
                    const auto promise = std::make_shared<Promise<void, std::error_code>>(mCore->eventLoop);

                    promise->getFuture().finally([=] {
                        event->set();
                    });

                    mCore->pending[SENDER].push_back(promise);
                    mCore->mutex.unlock();

                    if (const auto res = event->wait(timeout); !res) {
                        assert(res.error() == std::errc::timed_out);
                        std::lock_guard guard(mCore->mutex);
                        mCore->pending[SENDER].remove(promise);
                        result = tl::unexpected(SendSyncError::TIMEOUT);
                        break;
                    }

                    continue;
                }

                mCore->buffer[*index] = std::forward<U>(element);
                mCore->buffer.commit(*index);
                mCore->trigger(RECEIVER);

                break;
            }

            return result;
        }

        zero::async::coroutine::Task<void, SendError> send(T element) {
            if (mCore->closed)
                co_return tl::unexpected(SendError::DISCONNECTED);

            tl::expected<void, SendError> result;

            while (true) {
                const auto index = mCore->buffer.reserve();

                if (!index) {
                    mCore->mutex.lock();

                    if (mCore->closed) {
                        mCore->mutex.unlock();
                        result = tl::unexpected(SendError::DISCONNECTED);
                        break;
                    }

                    if (!mCore->buffer.full()) {
                        mCore->mutex.unlock();
                        continue;
                    }

                    const auto promise = std::make_shared<Promise<void, std::error_code>>(mCore->eventLoop);

                    mCore->pending[SENDER].push_back(promise);
                    mCore->mutex.unlock();

                    if (const auto res = co_await zero::async::coroutine::Cancellable{
                        promise->getFuture(),
                        [=]() -> tl::expected<void, std::error_code> {
                            if (promise->isFulfilled())
                                return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

                            promise->reject(zero::async::coroutine::Error::CANCELLED);
                            return {};
                        }
                    }; !res) {
                        assert(res.error() == std::errc::operation_canceled);
                        std::lock_guard guard(mCore->mutex);
                        mCore->pending[SENDER].remove(promise);
                        result = tl::unexpected(SendError::CANCELLED);
                        break;
                    }

                    continue;
                }

                mCore->buffer[*index] = std::move(element);
                mCore->buffer.commit(*index);
                mCore->trigger(RECEIVER);

                break;
            }

            co_return result;
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

    enum class TryReceiveError {
        DISCONNECTED = 1,
        EMPTY
    };

    class TryReceiveErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
        [[nodiscard]] std::error_condition default_error_condition(int value) const noexcept override;
    };

    std::error_code make_error_code(TryReceiveError e);

    enum class ReceiveSyncError {
        DISCONNECTED = 1,
        TIMEOUT
    };

    class ReceiveSyncErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
        [[nodiscard]] std::error_condition default_error_condition(int value) const noexcept override;
    };

    std::error_code make_error_code(ReceiveSyncError e);

    enum class ReceiveError {
        DISCONNECTED = 1,
        CANCELLED
    };

    class ReceiveErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
        [[nodiscard]] std::error_condition default_error_condition(int value) const noexcept override;
    };

    std::error_code make_error_code(ReceiveError e);

    template<typename T>
    class Receiver {
    public:
        explicit Receiver(std::shared_ptr<ChannelCore<T>> core) : mCore(std::move(core)) {
            ++mCore->counters[RECEIVER];
        }

        Receiver(const Receiver &rhs) : mCore(rhs.mCore) {
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

        tl::expected<T, TryReceiveError> tryReceive() {
            const auto index = mCore->buffer.acquire();

            if (!index)
                return tl::unexpected(mCore->closed ? TryReceiveError::DISCONNECTED : TryReceiveError::EMPTY);

            T element = std::move(mCore->buffer[*index]);

            mCore->buffer.release(*index);
            mCore->trigger(SENDER);

            return element;
        }

        tl::expected<T, ReceiveSyncError>
        receiveSync(const std::optional<std::chrono::milliseconds> timeout = std::nullopt) {
            tl::expected<T, ReceiveSyncError> result = tl::unexpected(ReceiveSyncError::DISCONNECTED);

            while (true) {
                const auto index = mCore->buffer.acquire();

                if (!index) {
                    mCore->mutex.lock();

                    if (mCore->closed) {
                        mCore->mutex.unlock();
                        break;
                    }

                    if (!mCore->buffer.empty()) {
                        mCore->mutex.unlock();
                        continue;
                    }

                    const auto event = std::make_shared<zero::atomic::Event>();
                    const auto promise = std::make_shared<Promise<void, std::error_code>>(mCore->eventLoop);

                    promise->getFuture().finally([=] {
                        event->set();
                    });

                    mCore->pending[RECEIVER].push_back(promise);
                    mCore->mutex.unlock();

                    if (const auto res = event->wait(timeout); !res) {
                        assert(res.error() == std::errc::timed_out);
                        std::lock_guard guard(mCore->mutex);
                        mCore->pending[RECEIVER].remove(promise);
                        result = tl::unexpected(ReceiveSyncError::TIMEOUT);
                        break;
                    }

                    continue;
                }

                result = std::move(mCore->buffer[*index]);

                mCore->buffer.release(*index);
                mCore->trigger(SENDER);

                break;
            }

            return result;
        }

        zero::async::coroutine::Task<T, ReceiveError> receive() {
            tl::expected<T, ReceiveError> result = tl::unexpected(ReceiveError::DISCONNECTED);

            while (true) {
                const auto index = mCore->buffer.acquire();

                if (!index) {
                    mCore->mutex.lock();

                    if (mCore->closed) {
                        mCore->mutex.unlock();
                        break;
                    }

                    if (!mCore->buffer.empty()) {
                        mCore->mutex.unlock();
                        continue;
                    }

                    const auto promise = std::make_shared<Promise<void, std::error_code>>(mCore->eventLoop);

                    mCore->pending[RECEIVER].push_back(promise);
                    mCore->mutex.unlock();

                    if (const auto res = co_await zero::async::coroutine::Cancellable{
                        promise->getFuture(),
                        [=]() -> tl::expected<void, std::error_code> {
                            if (promise->isFulfilled())
                                return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

                            promise->reject(zero::async::coroutine::Error::CANCELLED);
                            return {};
                        }
                    }; !res) {
                        assert(res.error() == std::errc::operation_canceled);
                        std::lock_guard guard(mCore->mutex);
                        mCore->pending[RECEIVER].remove(promise);
                        result = tl::unexpected(ReceiveError::CANCELLED);
                        break;
                    }

                    continue;
                }

                result = std::move(mCore->buffer[*index]);

                mCore->buffer.release(*index);
                mCore->trigger(SENDER);

                break;
            }

            co_return result;
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

template<>
struct std::is_error_code_enum<asyncio::TrySendError> : std::true_type {
};

template<>
struct std::is_error_code_enum<asyncio::SendSyncError> : std::true_type {
};

template<>
struct std::is_error_code_enum<asyncio::SendError> : std::true_type {
};

template<>
struct std::is_error_code_enum<asyncio::TryReceiveError> : std::true_type {
};

template<>
struct std::is_error_code_enum<asyncio::ReceiveSyncError> : std::true_type {
};

template<>
struct std::is_error_code_enum<asyncio::ReceiveError> : std::true_type {
};

#endif //ASYNCIO_CHANNEL_H

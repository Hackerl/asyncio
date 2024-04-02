#ifndef ASYNCIO_CHANNEL_H
#define ASYNCIO_CHANNEL_H

#include "promise.h"
#include <chrono>
#include <zero/interface.h>
#include <zero/async/coroutine.h>
#include <zero/atomic/event.h>
#include <zero/atomic/circular_buffer.h>

namespace asyncio {
    enum ChannelError {
        CHANNEL_EOF = 1,
        BROKEN_CHANNEL,
        EMPTY,
        FULL
    };

    class ChannelErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
        [[nodiscard]] std::error_condition default_error_condition(int value) const noexcept override;
    };

    const std::error_category &channelErrorCategory();
    std::error_code make_error_code(ChannelError e);
}

template<>
struct std::is_error_code_enum<asyncio::ChannelError> : std::true_type {
};

namespace asyncio {
    template<typename T>
    class ISender : public virtual zero::Interface {
    public:
        virtual tl::expected<void, std::error_code> trySend(T element) = 0;
        virtual tl::expected<void, std::error_code> sendSync(T element) = 0;
        virtual tl::expected<void, std::error_code> sendSync(T element, std::chrono::milliseconds timeout) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> send(T element) = 0;

        virtual zero::async::coroutine::Task<void, std::error_code>
        send(T element, std::chrono::milliseconds timeout) = 0;

        virtual void close() = 0;
    };

    template<typename T>
    class IReceiver : public virtual zero::Interface {
    public:
        virtual tl::expected<T, std::error_code> tryReceive() = 0;

        virtual tl::expected<T, std::error_code> receiveSync() = 0;
        virtual tl::expected<T, std::error_code> receiveSync(std::chrono::milliseconds timeout) = 0;

        virtual zero::async::coroutine::Task<T, std::error_code> receive() = 0;
        virtual zero::async::coroutine::Task<T, std::error_code> receive(std::chrono::milliseconds timeout) = 0;
    };

    template<typename T>
    class Channel final : public ISender<T>, public IReceiver<T> {
        static constexpr auto SENDER = 0;
        static constexpr auto RECEIVER = 1;

    public:
        explicit Channel(std::size_t capacity) : mClosed(false), mEventLoop(getEventLoop()), mBuffer(capacity) {
        }

        ~Channel() override {
            assert(mPending[SENDER].empty());
            assert(mPending[RECEIVER].empty());
        }

    private:
        tl::expected<void, std::error_code>
        sendSyncImpl(T &&element, const std::optional<std::chrono::milliseconds> timeout) {
            if (mClosed)
                return tl::unexpected(BROKEN_CHANNEL);

            tl::expected<void, std::error_code> result;

            while (true) {
                const auto index = mBuffer.reserve();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        result = tl::unexpected<std::error_code>(BROKEN_CHANNEL);
                        break;
                    }

                    if (!mBuffer.full()) {
                        mMutex.unlock();
                        continue;
                    }

                    const auto event = std::make_shared<zero::atomic::Event>();
                    const auto promise = std::make_shared<Promise<void, std::error_code>>(mEventLoop);

                    promise->getFuture().finally([=] {
                        event->set();
                    });

                    mPending[SENDER].push_back(promise);
                    mMutex.unlock();

                    if (const auto res = event->wait(timeout); !res) {
                        std::lock_guard guard(mMutex);
                        mPending[SENDER].remove(promise);
                        result = tl::unexpected(res.error());
                        break;
                    }

                    continue;
                }

                mBuffer[*index] = std::move(element);
                mBuffer.commit(*index);

                trigger<RECEIVER>();
                break;
            }

            return result;
        }

        zero::async::coroutine::Task<void, std::error_code>
        sendImpl(T element, const std::optional<std::chrono::milliseconds> timeout) {
            if (mClosed)
                co_return tl::unexpected(BROKEN_CHANNEL);

            tl::expected<void, std::error_code> result;

            while (true) {
                const auto index = mBuffer.reserve();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        result = tl::unexpected<std::error_code>(BROKEN_CHANNEL);
                        break;
                    }

                    if (!mBuffer.full()) {
                        mMutex.unlock();
                        continue;
                    }

                    const auto promise = std::make_shared<Promise<void, std::error_code>>(mEventLoop);

                    mPending[SENDER].push_back(promise);
                    mMutex.unlock();

                    if (const auto res = co_await asyncio::timeout(
                        zero::async::coroutine::from(zero::async::coroutine::Cancellable{
                            promise->getFuture(),
                            [=]() -> tl::expected<void, std::error_code> {
                                if (promise->isFulfilled())
                                    return tl::unexpected(make_error_code(std::errc::operation_not_supported));

                                promise->reject(make_error_code(std::errc::operation_canceled));
                                return {};
                            }
                        }),
                        timeout.value_or(std::chrono::milliseconds{0})
                    ).andThen([](const auto &r) -> tl::expected<void, std::error_code> {
                        if (!r)
                            return tl::unexpected(r.error());

                        return {};
                    }); !res) {
                        std::lock_guard guard(mMutex);
                        mPending[SENDER].remove(promise);
                        result = tl::unexpected(res.error());
                        break;
                    }

                    continue;
                }

                mBuffer[*index] = std::move(element);
                mBuffer.commit(*index);

                trigger<RECEIVER>();
                break;
            }

            co_return result;
        }

        tl::expected<T, std::error_code> receiveSyncImpl(const std::optional<std::chrono::milliseconds> timeout) {
            tl::expected<T, std::error_code> result;

            while (true) {
                const auto index = mBuffer.acquire();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        result = tl::unexpected<std::error_code>(CHANNEL_EOF);
                        break;
                    }

                    if (!mBuffer.empty()) {
                        mMutex.unlock();
                        continue;
                    }

                    const auto event = std::make_shared<zero::atomic::Event>();
                    const auto promise = std::make_shared<Promise<void, std::error_code>>(mEventLoop);

                    promise->getFuture().finally([=] {
                        event->set();
                    });

                    mPending[RECEIVER].push_back(promise);
                    mMutex.unlock();

                    if (const auto res = event->wait(timeout); !res) {
                        std::lock_guard guard(mMutex);
                        mPending[RECEIVER].remove(promise);
                        result = tl::unexpected(res.error());
                        break;
                    }

                    continue;
                }

                result = std::move(mBuffer[*index]);
                mBuffer.release(*index);

                trigger<SENDER>();
                break;
            }

            return result;
        }

        zero::async::coroutine::Task<T, std::error_code>
        receiveImpl(const std::optional<std::chrono::milliseconds> timeout) {
            tl::expected<T, std::error_code> result;

            while (true) {
                const auto index = mBuffer.acquire();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        result = tl::unexpected<std::error_code>(CHANNEL_EOF);
                        break;
                    }

                    if (!mBuffer.empty()) {
                        mMutex.unlock();
                        continue;
                    }

                    const auto promise = std::make_shared<Promise<void, std::error_code>>(mEventLoop);

                    mPending[RECEIVER].push_back(promise);
                    mMutex.unlock();

                    if (const auto res = co_await asyncio::timeout(
                        zero::async::coroutine::from(zero::async::coroutine::Cancellable{
                            promise->getFuture(),
                            [=]() -> tl::expected<void, std::error_code> {
                                if (promise->isFulfilled())
                                    return tl::unexpected(make_error_code(std::errc::operation_not_supported));

                                promise->reject(make_error_code(std::errc::operation_canceled));
                                return {};
                            }
                        }),
                        timeout.value_or(std::chrono::milliseconds{0})
                    ).andThen([](const auto &r) -> tl::expected<void, std::error_code> {
                        if (!r)
                            return tl::unexpected(r.error());

                        return {};
                    }); !res) {
                        std::lock_guard guard(mMutex);
                        mPending[RECEIVER].remove(promise);
                        result = tl::unexpected(res.error());
                        break;
                    }

                    continue;
                }

                T element = std::move(mBuffer[*index]);
                mBuffer.release(*index);

                trigger<SENDER>();
                result = std::move(element);

                break;
            }

            co_return result;
        }

        template<int Index>
        void trigger() {
            std::lock_guard guard(mMutex);

            if (mPending[Index].empty())
                return;

            for (const auto &promise: std::exchange(mPending[Index], {})) {
                if (promise->isFulfilled())
                    continue;

                promise->resolve();
            }
        }

        template<int Index>
        void trigger(const std::error_code &ec) {
            std::lock_guard guard(mMutex);

            if (mPending[Index].empty())
                return;

            for (const auto &promise: std::exchange(mPending[Index], {})) {
                if (promise->isFulfilled())
                    continue;

                promise->reject(ec);
            }
        }

    public:
        tl::expected<void, std::error_code> trySend(T element) override {
            if (mClosed)
                return tl::unexpected(BROKEN_CHANNEL);

            const auto index = mBuffer.reserve();

            if (!index)
                return tl::unexpected(FULL);

            mBuffer[*index] = std::move(element);
            mBuffer.commit(*index);

            trigger<RECEIVER>();
            return {};
        }

        tl::expected<void, std::error_code> sendSync(T element) override {
            return sendSyncImpl(std::move(element), std::nullopt);
        }

        tl::expected<void, std::error_code> sendSync(T element, std::chrono::milliseconds timeout) override {
            return sendSyncImpl(std::move(element), timeout);
        }

        zero::async::coroutine::Task<void, std::error_code> send(T element) override {
            return sendImpl(std::move(element), std::nullopt);
        }

        zero::async::coroutine::Task<void, std::error_code>
        send(T element, std::chrono::milliseconds timeout) override {
            return sendImpl(std::move(element), timeout);
        }

        void close() override {
            assert(!mClosed);

            {
                std::lock_guard guard(mMutex);

                if (mClosed)
                    return;

                mClosed = true;
            }

            trigger<SENDER>(BROKEN_CHANNEL);
            trigger<RECEIVER>(CHANNEL_EOF);
        }

        tl::expected<T, std::error_code> tryReceive() override {
            const auto index = mBuffer.acquire();

            if (!index)
                return tl::unexpected(
                    mClosed ? CHANNEL_EOF : EMPTY
                );

            T element = std::move(mBuffer[*index]);
            mBuffer.release(*index);

            trigger<SENDER>();
            return element;
        }

        tl::expected<T, std::error_code> receiveSync() override {
            return receiveSyncImpl(std::nullopt);
        }

        tl::expected<T, std::error_code> receiveSync(std::chrono::milliseconds timeout) override {
            return receiveSyncImpl(timeout);
        }

        zero::async::coroutine::Task<T, std::error_code> receive() override {
            return receiveImpl(std::nullopt);
        }

        zero::async::coroutine::Task<T, std::error_code> receive(std::chrono::milliseconds timeout) override {
            return receiveImpl(timeout);
        }

        [[nodiscard]] std::size_t size() const {
            return mBuffer.size();
        }

        [[nodiscard]] std::size_t capacity() const {
            return mBuffer.capacity();
        }

        [[nodiscard]] bool empty() const {
            return mBuffer.empty();
        }

        [[nodiscard]] bool full() const {
            return mBuffer.full();
        }

        [[nodiscard]] bool closed() const {
            return mClosed;
        }

    private:
        std::mutex mMutex;
        std::atomic<bool> mClosed;
        std::shared_ptr<EventLoop> mEventLoop;
        zero::atomic::CircularBuffer<T> mBuffer;
        std::array<std::list<std::shared_ptr<Promise<void, std::error_code>>>, 2> mPending;
    };

    template<typename T>
    using SenderPtr = std::shared_ptr<ISender<T>>;

    template<typename T>
    using ReceiverPtr = std::shared_ptr<IReceiver<T>>;

    template<typename T>
    using ChannelPtr = std::shared_ptr<Channel<T>>;

    template<typename T>
    ChannelPtr<T> makeChannel(const std::size_t capacity) {
        return std::make_shared<Channel<T>>(capacity);
    }
}

#endif //ASYNCIO_CHANNEL_H

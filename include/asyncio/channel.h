#ifndef ASYNCIO_CHANNEL_H
#define ASYNCIO_CHANNEL_H

#include "error.h"
#include "event_loop.h"
#include <chrono>
#include <zero/interface.h>
#include <zero/async/coroutine.h>
#include <zero/atomic/event.h>
#include <zero/atomic/circular_buffer.h>

namespace asyncio {
    template<typename T>
    class ISender : public virtual zero::Interface {
    public:
        virtual tl::expected<void, std::error_code> trySend(const T &element) = 0;
        virtual tl::expected<void, std::error_code> trySend(T &&element) = 0;

    public:
        virtual tl::expected<void, std::error_code> sendSync(const T &element) = 0;
        virtual tl::expected<void, std::error_code> sendSync(T &&element) = 0;
        virtual tl::expected<void, std::error_code> sendSync(const T &element, std::chrono::milliseconds timeout) = 0;
        virtual tl::expected<void, std::error_code> sendSync(T &&element, std::chrono::milliseconds timeout) = 0;

    public:
        virtual zero::async::coroutine::Task<void, std::error_code> send(const T &element) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> send(T &&element) = 0;

        virtual zero::async::coroutine::Task<void, std::error_code>
        send(const T &element, std::chrono::milliseconds timeout) = 0;

        virtual zero::async::coroutine::Task<void, std::error_code>
        send(T &&element, std::chrono::milliseconds timeout) = 0;

    public:
        virtual void close() = 0;
    };

    template<typename T>
    class IReceiver : public virtual zero::Interface {
    public:
        virtual tl::expected<T, std::error_code> receiveSync() = 0;
        virtual tl::expected<T, std::error_code> receiveSync(std::chrono::milliseconds timeout) = 0;

    public:
        virtual zero::async::coroutine::Task<T, std::error_code> receive() = 0;
        virtual zero::async::coroutine::Task<T, std::error_code> receive(std::chrono::milliseconds timeout) = 0;

    public:
        virtual tl::expected<T, std::error_code> tryReceive() = 0;
    };

    template<typename T>
    class Channel : public ISender<T>, public IReceiver<T> {
    private:
        static constexpr auto SENDER = 0;
        static constexpr auto RECEIVER = 1;

    public:
        explicit Channel(size_t capacity) : mClosed(false), mBuffer(capacity), mEventLoop(getEventLoop()) {

        }

    public:
        tl::expected<void, std::error_code> trySend(const T &element) override {
            return trySendImpl(element);
        }

        tl::expected<void, std::error_code> trySend(T &&element) override {
            return trySendImpl(std::move(element));
        }

        tl::expected<void, std::error_code> sendSync(const T &element) override {
            return sendSyncImpl(element, std::nullopt);
        }

        tl::expected<void, std::error_code> sendSync(const T &element, std::chrono::milliseconds timeout) override {
            return sendSyncImpl(element, timeout);
        }

        tl::expected<void, std::error_code> sendSync(T &&element) override {
            return sendSyncImpl(std::move(element), std::nullopt);
        }

        tl::expected<void, std::error_code> sendSync(T &&element, std::chrono::milliseconds timeout) override {
            return sendSyncImpl(std::move(element), timeout);
        }

        zero::async::coroutine::Task<void, std::error_code> send(const T &element) override {
            return sendImpl(element, std::nullopt);
        }

        zero::async::coroutine::Task<void, std::error_code> send(T &&element) override {
            return sendImpl(std::move(element), std::nullopt);
        }

        zero::async::coroutine::Task<void, std::error_code>
        send(const T &element, std::chrono::milliseconds timeout) override {
            return sendImpl(element, timeout);
        }

        zero::async::coroutine::Task<void, std::error_code>
        send(T &&element, std::chrono::milliseconds timeout) override {
            return sendImpl(std::move(element), timeout);
        }

    public:
        void close() override {
            {
                std::lock_guard<std::mutex> guard(mMutex);

                if (mClosed)
                    return;

                mClosed = true;
            }

            trigger<SENDER>(Error::IO_EOF);
            trigger<RECEIVER>(Error::IO_EOF);
        }

    public:
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

        tl::expected<T, std::error_code> tryReceive() override {
            auto index = mBuffer.acquire();

            if (!index)
                return tl::unexpected(
                        mClosed ?
                        make_error_code(Error::IO_EOF) :
                        make_error_code(std::errc::operation_would_block)
                );

            T element = std::move(mBuffer[*index]);
            mBuffer.release(*index);

            trigger<SENDER>();
            return element;
        }

    private:
        template<typename U>
        tl::expected<void, std::error_code> trySendImpl(U &&element) {
            if (mClosed)
                return tl::unexpected(Error::IO_EOF);

            auto index = mBuffer.reserve();

            if (!index)
                return tl::unexpected(make_error_code(std::errc::operation_would_block));

            mBuffer[*index] = std::forward<U>(element);
            mBuffer.commit(*index);

            trigger<RECEIVER>();
            return {};
        }

        template<typename U>
        tl::expected<void, std::error_code>
        sendSyncImpl(U &&element, std::optional<std::chrono::milliseconds> timeout) {
            if (mClosed)
                return tl::unexpected(Error::IO_EOF);

            tl::expected<void, std::error_code> result;

            while (true) {
                auto index = mBuffer.reserve();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        result = tl::unexpected<std::error_code>(Error::IO_EOF);
                        break;
                    }

                    if (!mBuffer.full()) {
                        mMutex.unlock();
                        continue;
                    }

                    auto event = std::make_shared<zero::atomic::Event>();
                    zero::async::promise::Promise<void, std::error_code> promise;

                    promise.finally([=]() {
                        event->notify();
                    });

                    mPending[SENDER].push_back(promise);
                    mMutex.unlock();

                    auto res = event->wait(timeout);

                    if (!res) {
                        std::lock_guard<std::mutex> guard(mMutex);
                        mPending[SENDER].remove(promise);
                        result = tl::unexpected(res.error());
                        break;
                    }

                    continue;
                }

                mBuffer[*index] = std::forward<U>(element);
                mBuffer.commit(*index);

                trigger<RECEIVER>();
                break;
            }

            return result;
        }

        zero::async::coroutine::Task<void, std::error_code>
        sendImpl(T element, std::optional<std::chrono::milliseconds> timeout) {
            if (mClosed)
                co_return tl::unexpected(Error::IO_EOF);

            tl::expected<void, std::error_code> result;

            while (true) {
                auto index = mBuffer.reserve();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        result = tl::unexpected<std::error_code>(Error::IO_EOF);
                        break;
                    }

                    if (!mBuffer.full()) {
                        mMutex.unlock();
                        continue;
                    }

                    zero::async::promise::Promise<void, std::error_code> promise;

                    mPending[SENDER].push_back(promise);
                    mMutex.unlock();

                    auto task = [](auto promise) -> zero::async::coroutine::Task<void, std::error_code> {
                        co_return co_await zero::async::coroutine::Cancellable{
                                promise,
                                [=]() mutable -> tl::expected<void, std::error_code> {
                                    promise.reject(make_error_code(std::errc::operation_canceled));
                                    return {};
                                }
                        };
                    }(promise);

                    auto res = timeout ? (co_await asyncio::timeout(task, *timeout)) : (co_await task);

                    if (!res) {
                        std::lock_guard<std::mutex> guard(mMutex);
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

    private:
        tl::expected<T, std::error_code> receiveSyncImpl(std::optional<std::chrono::milliseconds> timeout) {
            tl::expected<T, std::error_code> result;

            while (true) {
                auto index = mBuffer.acquire();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        result = tl::unexpected<std::error_code>(Error::IO_EOF);
                        break;
                    }

                    if (!mBuffer.empty()) {
                        mMutex.unlock();
                        continue;
                    }

                    auto event = std::make_shared<zero::atomic::Event>();
                    zero::async::promise::Promise<void, std::error_code> promise;

                    promise.finally([=]() {
                        event->notify();
                    });

                    mPending[RECEIVER].push_back(promise);
                    mMutex.unlock();

                    auto res = event->wait(timeout);

                    if (!res) {
                        std::lock_guard<std::mutex> guard(mMutex);
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

        zero::async::coroutine::Task<T, std::error_code> receiveImpl(std::optional<std::chrono::milliseconds> timeout) {
            tl::expected<T, std::error_code> result;

            while (true) {
                auto index = mBuffer.acquire();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        result = tl::unexpected<std::error_code>(Error::IO_EOF);
                        break;
                    }

                    if (!mBuffer.empty()) {
                        mMutex.unlock();
                        continue;
                    }

                    zero::async::promise::Promise<void, std::error_code> promise;

                    mPending[RECEIVER].push_back(promise);
                    mMutex.unlock();

                    auto task = [](auto promise) -> zero::async::coroutine::Task<void, std::error_code> {
                        co_return co_await zero::async::coroutine::Cancellable{
                                promise,
                                [=]() mutable -> tl::expected<void, std::error_code> {
                                    promise.reject(make_error_code(std::errc::operation_canceled));
                                    return {};
                                }
                        };
                    }(promise);

                    auto res = timeout ? (co_await asyncio::timeout(task, *timeout)) : (co_await task);

                    if (!res) {
                        std::lock_guard<std::mutex> guard(mMutex);
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

    private:
        template<int Index>
        void trigger() {
            std::lock_guard<std::mutex> guard(mMutex);

            if (mPending[Index].empty())
                return;

            mEventLoop->post([=, pending = std::exchange(mPending[Index], {})]() mutable {
                for (auto &promise: pending)
                    promise.resolve();
            });
        }

        template<int Index>
        void trigger(const std::error_code &ec) {
            std::lock_guard<std::mutex> guard(mMutex);

            if (mPending[Index].empty())
                return;

            mEventLoop->post([=, pending = std::exchange(mPending[Index], {})]() mutable {
                for (auto &promise: pending)
                    promise.reject(ec);
            });
        }

    private:
        std::mutex mMutex;
        std::atomic<bool> mClosed;
        std::shared_ptr<EventLoop> mEventLoop;
        zero::atomic::CircularBuffer<T> mBuffer;
        std::array<std::list<zero::async::promise::Promise<void, std::error_code>>, 2> mPending;
    };
}

#endif //ASYNCIO_CHANNEL_H

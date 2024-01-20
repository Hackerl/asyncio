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

    private:
        tl::expected<void, std::error_code>
        sendSyncImpl(T &&element, const std::optional<std::chrono::milliseconds> timeout) {
            if (mClosed)
                return tl::unexpected(make_error_code(std::errc::broken_pipe));

            tl::expected<void, std::error_code> result;

            while (true) {
                const auto index = mBuffer.reserve();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        result = tl::unexpected(make_error_code(std::errc::broken_pipe));
                        break;
                    }

                    if (!mBuffer.full()) {
                        mMutex.unlock();
                        continue;
                    }

                    const auto event = std::make_shared<zero::atomic::Event>();
                    const auto promise = zero::async::promise::make<void, std::error_code>();

                    promise->finally([=] {
                        event->notify();
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
                co_return tl::unexpected(make_error_code(std::errc::broken_pipe));

            tl::expected<void, std::error_code> result;

            while (true) {
                const auto index = mBuffer.reserve();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        result = tl::unexpected(make_error_code(std::errc::broken_pipe));
                        break;
                    }

                    if (!mBuffer.full()) {
                        mMutex.unlock();
                        continue;
                    }

                    const auto promise = zero::async::promise::make<void, std::error_code>();

                    mPending[SENDER].push_back(promise);
                    mMutex.unlock();

                    if (const auto res = co_await asyncio::timeout(
                        zero::async::coroutine::from(zero::async::coroutine::Cancellable{
                            promise,
                            [=]() -> tl::expected<void, std::error_code> {
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
                        result = tl::unexpected<std::error_code>(IO_EOF);
                        break;
                    }

                    if (!mBuffer.empty()) {
                        mMutex.unlock();
                        continue;
                    }

                    const auto event = std::make_shared<zero::atomic::Event>();
                    const auto promise = zero::async::promise::make<void, std::error_code>();

                    promise->finally([=] {
                        event->notify();
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
                        result = tl::unexpected<std::error_code>(IO_EOF);
                        break;
                    }

                    if (!mBuffer.empty()) {
                        mMutex.unlock();
                        continue;
                    }

                    const auto promise = zero::async::promise::make<void, std::error_code>();

                    mPending[RECEIVER].push_back(promise);
                    mMutex.unlock();

                    if (const auto res = co_await asyncio::timeout(
                        zero::async::coroutine::from(zero::async::coroutine::Cancellable{
                            promise,
                            [=]() -> tl::expected<void, std::error_code> {
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

            mEventLoop->post([pending = std::exchange(mPending[Index], {})] {
                for (const auto &promise: pending)
                    promise->resolve();
            });
        }

        template<int Index>
        void trigger(const std::error_code &ec) {
            std::lock_guard guard(mMutex);

            if (mPending[Index].empty())
                return;

            mEventLoop->post([=, pending = std::exchange(mPending[Index], {})] {
                for (const auto &promise: pending)
                    promise->reject(ec);
            });
        }

    public:
        tl::expected<void, std::error_code> trySend(T element) override {
            if (mClosed)
                return tl::unexpected(make_error_code(std::errc::broken_pipe));

            const auto index = mBuffer.reserve();

            if (!index)
                return tl::unexpected(make_error_code(std::errc::operation_would_block));

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
            {
                std::lock_guard guard(mMutex);

                if (mClosed)
                    return;

                mClosed = true;
            }

            trigger<SENDER>(make_error_code(std::errc::broken_pipe));
            trigger<RECEIVER>(IO_EOF);
        }

        tl::expected<T, std::error_code> tryReceive() override {
            const auto index = mBuffer.acquire();

            if (!index)
                return tl::unexpected(
                    mClosed ? make_error_code(IO_EOF) : make_error_code(std::errc::operation_would_block)
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

    private:
        std::mutex mMutex;
        std::atomic<bool> mClosed;
        std::shared_ptr<EventLoop> mEventLoop;
        zero::atomic::CircularBuffer<T> mBuffer;
        std::array<std::list<zero::async::promise::PromisePtr<void, std::error_code>>, 2> mPending;
    };
}

#endif //ASYNCIO_CHANNEL_H

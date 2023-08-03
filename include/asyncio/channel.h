#ifndef ASYNCIO_CHANNEL_H
#define ASYNCIO_CHANNEL_H

#include "error.h"
#include "ev/event.h"
#include "event_loop.h"
#include <chrono>
#include <variant>
#include <zero/interface.h>
#include <zero/async/coroutine.h>
#include <zero/atomic/event.h>
#include <zero/atomic/circular_buffer.h>

namespace asyncio {
    template<typename T>
    class ISender : public virtual zero::Interface {
    public:
        virtual tl::expected<void, std::error_code> trySend(const T &element) = 0;
        virtual tl::expected<void, std::error_code> sendSync(const T &element) = 0;
        virtual tl::expected<void, std::error_code> sendSync(const T &element, std::chrono::milliseconds timeout) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> send(const T &element) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> send(
                const T &element,
                std::chrono::milliseconds timeout
        ) = 0;

    public:
        virtual tl::expected<void, std::error_code> trySend(T &&element) = 0;
        virtual tl::expected<void, std::error_code> sendSync(T &&element) = 0;
        virtual tl::expected<void, std::error_code> sendSync(T &&element, std::chrono::milliseconds timeout) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> send(T &&element) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> send(
                T &&element,
                std::chrono::milliseconds timeout
        ) = 0;

    public:
        virtual void close() = 0;
    };

    template<typename T>
    class IReceiver : public virtual zero::Interface {
    public:
        virtual tl::expected<T, std::error_code> receiveSync() = 0;
        virtual tl::expected<T, std::error_code> receiveSync(std::chrono::milliseconds timeout) = 0;
        virtual tl::expected<T, std::error_code> tryReceive() = 0;
        virtual zero::async::coroutine::Task<T, std::error_code> receive() = 0;
        virtual zero::async::coroutine::Task<T, std::error_code> receive(std::chrono::milliseconds timeout) = 0;
    };

    template<typename T>
    class IChannel : public ISender<T>, public IReceiver<T> {

    };

    template<typename T>
    class Channel : public IChannel<T> {
    private:
        static constexpr auto SENDER = 0;
        static constexpr auto RECEIVER = 1;

    private:
        using Event = std::variant<std::shared_ptr<ev::Event>, std::shared_ptr<zero::atomic::Event>>;

    public:
        explicit Channel(size_t capacity) : mClosed(false), mBuffer(capacity), mEventLoop(getEventLoop()) {

        }

        Channel(const Channel &) = delete;
        Channel &operator=(const Channel &) = delete;

    public:
        tl::expected<void, std::error_code> trySend(const T &element) override {
            T e = element;
            return trySend(std::move(e));
        }

        tl::expected<void, std::error_code> sendSync(const T &element) override {
            T e = element;
            return sendSync(std::move(e), std::nullopt);
        }

        tl::expected<void, std::error_code> sendSync(const T &element, std::chrono::milliseconds timeout) override {
            T e = element;
            return sendSync(std::move(e), std::make_optional<std::chrono::milliseconds>(timeout));
        }

        zero::async::coroutine::Task<void, std::error_code> send(const T &element) override {
            T e = element;
            return send(std::move(e));
        }

        zero::async::coroutine::Task<void, std::error_code>
        send(const T &element, std::chrono::milliseconds timeout) override {
            T e = element;
            return send(std::move(e), std::make_optional<std::chrono::milliseconds>(timeout));
        }

        tl::expected<void, std::error_code> trySend(T &&element) override {
            if (mClosed)
                return tl::unexpected(Error::IO_EOF);

            std::optional<size_t> index = mBuffer.reserve();

            if (!index)
                return tl::unexpected(make_error_code(std::errc::operation_would_block));

            mBuffer[*index] = std::move(element);
            mBuffer.commit(*index);

            std::lock_guard<std::mutex> guard(mMutex);

            trigger<RECEIVER>(ev::READ);
            return {};
        }

        tl::expected<void, std::error_code> sendSync(T &&element) override {
            return sendSync(std::move(element), std::nullopt);
        }

        tl::expected<void, std::error_code> sendSync(T &&element, std::chrono::milliseconds timeout) override {
            return sendSync(std::move(element), std::make_optional<std::chrono::milliseconds>(timeout));
        }

        zero::async::coroutine::Task<void, std::error_code> send(T &&element) override {
            return send(std::move(element), std::nullopt);
        }

        zero::async::coroutine::Task<void, std::error_code>
        send(T &&element, std::chrono::milliseconds timeout) override {
            return send(std::move(element), std::make_optional<std::chrono::milliseconds>(timeout));
        }

        void close() override {
            std::lock_guard<std::mutex> guard(mMutex);

            if (mClosed)
                return;

            mClosed = true;

            trigger<SENDER>(ev::CLOSED);
            trigger<RECEIVER>(ev::CLOSED);
        }

        tl::expected<T, std::error_code> receiveSync() override {
            return receiveSync(std::nullopt);
        }

        tl::expected<T, std::error_code> receiveSync(std::chrono::milliseconds timeout) override {
            return receiveSync(std::make_optional<std::chrono::milliseconds>(timeout));
        }

        tl::expected<T, std::error_code> tryReceive() override {
            std::optional<size_t> index = mBuffer.acquire();

            if (!index)
                return tl::unexpected(
                        mClosed ?
                        make_error_code(Error::IO_EOF) :
                        make_error_code(std::errc::operation_would_block)
                );

            T element = std::move(mBuffer[*index]);
            mBuffer.release(*index);

            std::lock_guard<std::mutex> guard(mMutex);

            trigger<SENDER>(ev::WRITE);
            return element;
        }

        zero::async::coroutine::Task<T, std::error_code> receive() override {
            return receive(std::nullopt);
        }

        zero::async::coroutine::Task<T, std::error_code> receive(std::chrono::milliseconds timeout) override {
            return receive(std::make_optional<std::chrono::milliseconds>(timeout));
        }

    private:
        tl::expected<void, std::error_code> sendSync(T &&element, std::optional<std::chrono::milliseconds> timeout) {
            if (mClosed)
                return tl::unexpected(Error::IO_EOF);

            tl::expected<void, std::error_code> result;

            while (true) {
                std::optional<size_t> index = mBuffer.reserve();

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

                    std::shared_ptr<zero::atomic::Event> event = std::make_shared<zero::atomic::Event>();

                    mPending[SENDER].emplace_back(event);
                    mMutex.unlock();

                    auto res = event->wait(timeout);

                    if (!res) {
                        result = tl::unexpected(res.error());
                        break;
                    }

                    continue;
                }

                mBuffer[*index] = std::move(element);
                mBuffer.commit(*index);

                std::lock_guard<std::mutex> guard(mMutex);

                trigger<RECEIVER>(ev::READ);
                break;
            }

            return result;
        }

        zero::async::coroutine::Task<void, std::error_code>
        send(T &&element, std::optional<std::chrono::milliseconds> timeout) {
            if (mClosed)
                co_return tl::unexpected(Error::IO_EOF);

            tl::expected<void, std::error_code> result;

            while (true) {
                std::optional<size_t> index = mBuffer.reserve();

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

                    std::shared_ptr<ev::Event> event = getEvent();

                    mPending[SENDER].emplace_back(event);
                    mMutex.unlock();

                    auto res = co_await event->on(ev::WRITE, timeout);

                    if (!res) {
                        result = tl::unexpected(res.error());
                        break;
                    }

                    short what = res.value();

                    if (what & ev::CLOSED) {
                        result = tl::unexpected<std::error_code>(Error::IO_EOF);
                        break;
                    } else if (what & ev::TIMEOUT) {
                        result = tl::unexpected(make_error_code(std::errc::timed_out));
                        break;
                    }

                    continue;
                }

                mBuffer[*index] = std::move(element);
                mBuffer.commit(*index);

                std::lock_guard<std::mutex> guard(mMutex);

                trigger<RECEIVER>(ev::READ);
                break;
            }

            co_return result;
        }

        tl::expected<T, std::error_code> receiveSync(std::optional<std::chrono::milliseconds> timeout) {
            tl::expected<T, std::error_code> result;

            while (true) {
                std::optional<size_t> index = mBuffer.acquire();

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

                    std::shared_ptr<zero::atomic::Event> event = std::make_shared<zero::atomic::Event>();

                    mPending[RECEIVER].emplace_back(event);
                    mMutex.unlock();

                    auto res = event->wait(timeout);

                    if (!res) {
                        result = tl::unexpected(res.error());
                        break;
                    }

                    continue;
                }

                result = std::move(mBuffer[*index]);
                mBuffer.release(*index);

                std::lock_guard<std::mutex> guard(mMutex);

                trigger<SENDER>(ev::WRITE);
                break;
            }

            return result;
        }

        zero::async::coroutine::Task<T, std::error_code> receive(std::optional<std::chrono::milliseconds> timeout) {
            tl::expected<T, std::error_code> result;

            while (true) {
                std::optional<size_t> index = mBuffer.acquire();

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

                    std::shared_ptr<ev::Event> event = getEvent();

                    mPending[RECEIVER].emplace_back(event);
                    mMutex.unlock();

                    auto res = co_await event->on(ev::READ, timeout);

                    if (!res) {
                        result = tl::unexpected(res.error());
                        break;
                    }

                    short what = res.value();

                    if (what & ev::CLOSED) {
                        result = tl::unexpected<std::error_code>(Error::IO_EOF);
                        break;
                    } else if (what & ev::TIMEOUT) {
                        result = tl::unexpected(make_error_code(std::errc::timed_out));
                        break;
                    }

                    continue;
                }

                T element = std::move(mBuffer[*index]);
                mBuffer.release(*index);

                std::lock_guard<std::mutex> guard(mMutex);

                trigger<SENDER>(ev::WRITE);
                result = std::move(element);

                break;
            }

            co_return result;
        }

    private:
        template<int Index>
        void trigger(short what) {
            if (mPending[Index].empty())
                return;

            mEventLoop->post([=, pending = std::move(mPending[Index])]() {
                for (const auto &event: pending) {
                    if (event.index() != 0) {
                        std::get<1>(event)->notify();
                        continue;
                    }

                    auto &evt = std::get<0>(event);

                    if (!evt->pending())
                        continue;

                    evt->trigger(what);
                }
            });
        }

        std::shared_ptr<ev::Event> getEvent() {
            auto it = std::find_if(mEvents.begin(), mEvents.end(), [](const auto &event) {
                return event.use_count() == 1;
            });

            if (it == mEvents.end()) {
                mEvents.push_back(std::make_shared<ev::Event>(-1));
                return mEvents.back();
            }

            return *it;
        }

    private:
        std::mutex mMutex;
        std::atomic<bool> mClosed;
        std::shared_ptr<EventLoop> mEventLoop;
        zero::atomic::CircularBuffer<T> mBuffer;
        std::list<std::shared_ptr<ev::Event>> mEvents;
        std::array<std::list<Event>, 2> mPending;
    };
}

#endif //ASYNCIO_CHANNEL_H

#ifndef ASYNCIO_FUTURE_H
#define ASYNCIO_FUTURE_H

#include "event_loop.h"
#include <cassert>

namespace asyncio {
    template<typename T>
    class Future {
    public:
        Future() : mEventLoop(getEventLoop()) {
        }

        explicit Future(std::shared_ptr<EventLoop> eventLoop): mEventLoop(std::move(eventLoop)) {
        }

        Future(const Future &) = delete;
        Future &operator=(const Future &) = delete;

        [[nodiscard]] bool done() const {
            return mResult.has_value();
        }

        template<typename... Ts>
        void set(Ts &&... args) {
            assert(!mResult);
            mResult = tl::expected<T, std::error_code>(std::forward<Ts>(args)...);

            for (auto &promise: std::exchange(mPending, {})) {
                mEventLoop->post([promise = std::move(promise)] {
                    promise->resolve();
                });
            }
        }

        void setError(const std::error_code &ec) {
            assert(!mResult);
            mResult = tl::unexpected(ec);

            for (auto &promise: std::exchange(mPending, {})) {
                mEventLoop->post([promise = std::move(promise)] {
                    promise->resolve();
                });
            }
        }

        zero::async::coroutine::Task<T, std::error_code> get() {
            if (mResult)
                co_return *mResult;

            const auto promise = zero::async::promise::make<void, std::error_code>();
            mPending.push_back(promise);

            const auto result = co_await zero::async::coroutine::Cancellable{
                promise,
                [=, this]() -> tl::expected<void, std::error_code> {
                    mPending.remove(promise);
                    promise->reject(make_error_code(std::errc::operation_canceled));
                    return {};
                }
            };
            CO_EXPECT(result);

            co_return *mResult;
        }

        zero::async::coroutine::Task<T, std::error_code> get(std::optional<std::chrono::milliseconds> ms) {
            if (!ms)
                co_return co_await get();

            auto result = co_await timeout(get(), *ms);
            CO_EXPECT(result);

            co_return std::move(*result);
        }

    private:
        std::shared_ptr<EventLoop> mEventLoop;
        std::optional<tl::expected<T, std::error_code>> mResult;
        std::list<zero::async::promise::PromisePtr<void, std::error_code>> mPending;
    };

    template<typename T>
    using FuturePtr = std::shared_ptr<Future<T>>;

    template<typename T>
    using FutureConstPtr = std::shared_ptr<const Future<T>>;

    template<typename T>
    FuturePtr<T> makeFuture() {
        return std::make_shared<Future<T>>();
    }
}

#endif //ASYNCIO_FUTURE_H

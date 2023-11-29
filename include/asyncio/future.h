#ifndef ASYNCIO_FUTURE_H
#define ASYNCIO_FUTURE_H

#include "event_loop.h"
#include <cassert>

namespace asyncio {
    template<typename T>
    class Future {
        struct Storage {
            std::optional<tl::expected<T, std::error_code>> result;
            std::list<zero::async::promise::Promise<void, std::error_code>> pending;
        };

    public:
        Future() : mEventLoop(getEventLoop()), mStorage(std::make_shared<Storage>()) {
        }

        explicit Future(std::shared_ptr<EventLoop> eventLoop)
            : mEventLoop(std::move(eventLoop)), mStorage(std::make_shared<Storage>()) {
        }

        auto operator<=>(const Future &) const = default;

        [[nodiscard]] bool done() const {
            return mStorage->result.has_value();
        }

        template<typename... Ts>
        void set(Ts &&... args) {
            assert(!mStorage->result);
            mStorage->result = tl::expected<T, std::error_code>(std::forward<Ts>(args)...);

            for (auto &promise : std::exchange(mStorage->pending, {})) {
                mEventLoop->post([promise = std::move(promise)]() mutable {
                    promise.resolve();
                });
            }
        }

        void setError(const std::error_code &ec) {
            assert(!mStorage->result);
            mStorage->result = tl::unexpected(ec);

            for (auto &promise : std::exchange(mStorage->pending, {})) {
                mEventLoop->post([promise = std::move(promise)]() mutable {
                    promise.resolve();
                });
            }
        }

        zero::async::coroutine::Task<T, std::error_code> get() {
            if (mStorage->result)
                co_return *mStorage->result;

            zero::async::promise::Promise<void, std::error_code> promise;
            mStorage->pending.push_back(promise);

            CO_TRY(co_await zero::async::coroutine::Cancellable{
                promise,
                [=, this]() mutable -> tl::expected<void, std::error_code> {
                    mStorage->pending.remove(promise);
                    promise.reject(make_error_code(std::errc::operation_canceled));
                    return {};
                }
            });

            co_return *mStorage->result;
        }

        zero::async::coroutine::Task<T, std::error_code> get(std::optional<std::chrono::milliseconds> ms) {
            if (!ms)
                co_return std::move(co_await get());

            auto result = CO_TRY(std::move(co_await timeout(get(), *ms)));
            co_return std::move(*result);
        }

    private:
        std::shared_ptr<EventLoop> mEventLoop;
        std::shared_ptr<Storage> mStorage;
    };
}

#endif //ASYNCIO_FUTURE_H

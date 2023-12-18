#ifndef ASYNCIO_CONDITION_H
#define ASYNCIO_CONDITION_H

#include "mutex.h"

namespace asyncio::sync {
    class Condition {
    public:
        zero::async::coroutine::Task<void, std::error_code>
        wait(Mutex &mutex, std::optional<std::chrono::milliseconds> ms = std::nullopt);

        template<typename F>
            requires std::is_invocable_v<F>
        zero::async::coroutine::Task<void, std::error_code>
        wait(Mutex &mutex, F predicate, const std::optional<std::chrono::milliseconds> ms = std::nullopt) {
            while (!predicate()) {
                CO_TRY(co_await wait(mutex, ms));
            }

            co_return tl::expected<void, std::error_code>{};
        }

        void notify();
        void broadcast();

    private:
        std::list<Future<void>> mPending;
    };
}

#endif //ASYNCIO_CONDITION_H

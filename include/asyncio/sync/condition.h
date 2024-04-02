#ifndef ASYNCIO_CONDITION_H
#define ASYNCIO_CONDITION_H

#include "mutex.h"

namespace asyncio::sync {
    class Condition {
    public:
        zero::async::coroutine::Task<void, std::error_code>
        wait(Mutex &mutex, std::optional<std::chrono::milliseconds> timeout = std::nullopt);

        template<typename F>
            requires std::is_invocable_v<F>
        zero::async::coroutine::Task<void, std::error_code>
        wait(Mutex &mutex, F predicate, const std::optional<std::chrono::milliseconds> timeout = std::nullopt) {
            while (!predicate()) {
                CO_EXPECT(co_await wait(mutex, timeout));
            }

            co_return tl::expected<void, std::error_code>{};
        }

        void notify();
        void broadcast();

    private:
        std::atomic<int> mCounter;
        std::list<std::shared_ptr<Promise<void, std::error_code>>> mPending;
    };
}

#endif //ASYNCIO_CONDITION_H

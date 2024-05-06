#ifndef ASYNCIO_CONDITION_H
#define ASYNCIO_CONDITION_H

#include "mutex.h"

namespace asyncio::sync {
    class Condition {
    public:
        zero::async::coroutine::Task<void, std::error_code> wait(Mutex &mutex);

        template<typename F>
            requires std::is_invocable_v<F>
        zero::async::coroutine::Task<void, std::error_code> wait(Mutex &mutex, F predicate) {
            while (!predicate()) {
                CO_EXPECT(co_await wait(mutex));
            }

            co_return {};
        }

        void notify();
        void broadcast();

    private:
        std::atomic<int> mCounter;
        std::list<std::shared_ptr<Promise<void, std::error_code>>> mPending;
    };
}

#endif //ASYNCIO_CONDITION_H

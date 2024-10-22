#ifndef ASYNCIO_CONDITION_H
#define ASYNCIO_CONDITION_H

#include "mutex.h"

namespace asyncio::sync {
    class Condition {
    public:
        task::Task<void, std::error_code> wait(Mutex &mutex);

        template<typename F>
            requires std::is_same_v<std::invoke_result_t<F>, bool>
        task::Task<void, std::error_code> wait(Mutex &mutex, F predicate) {
            while (!predicate()) {
                CO_EXPECT(co_await wait(mutex));
            }

            co_return {};
        }

        void notify();
        void broadcast();

    private:
        int mCounter{};
        std::list<std::shared_ptr<Promise<void, std::error_code>>> mPending;
    };
}

#endif //ASYNCIO_CONDITION_H

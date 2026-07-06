#ifndef ASYNCIO_CONDITION_H
#define ASYNCIO_CONDITION_H

#include "mutex.h"

namespace asyncio::sync {
    class Condition {
    public:
        Condition() = default;

        Condition(const Condition &rhs) = delete;
        Condition(Condition &&rhs) = default;

        Condition &operator=(const Condition &rhs) = delete;
        Condition &operator=(Condition &&rhs) noexcept = default;

        task::Task<void, std::error_code> wait(Mutex &mutex);
        task::Task<void, std::error_code> wait(Mutex &mutex, std::function<bool()> predicate);

        void notify();
        void broadcast();

    private:
        int mCounter{};
        std::list<std::shared_ptr<Promise<void, std::error_code>>> mPending;
    };
}

#endif //ASYNCIO_CONDITION_H

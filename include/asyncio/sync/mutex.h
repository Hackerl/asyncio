#ifndef ASYNCIO_MUTEX_H
#define ASYNCIO_MUTEX_H

#include <asyncio/task.h>

namespace asyncio::sync {
    class Mutex {
        void wakeup() const;

    public:
        task::Task<void, std::error_code> lock();
        void unlock();

        [[nodiscard]] bool locked() const;

    private:
        bool mLocked{false};
        std::list<std::shared_ptr<Promise<void, std::error_code>>> mPending;
    };
}

#endif //ASYNCIO_MUTEX_H

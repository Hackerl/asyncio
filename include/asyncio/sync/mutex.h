#ifndef ASYNCIO_MUTEX_H
#define ASYNCIO_MUTEX_H

#include <atomic>
#include <asyncio/promise.h>

namespace asyncio::sync {
    class Mutex {
        void wakeup()const;

    public:
        zero::async::coroutine::Task<void, std::error_code> lock();
        void unlock();

        [[nodiscard]] bool locked() const;

    private:
        std::atomic<bool> mLocked;
        std::list<std::shared_ptr<Promise<void, std::error_code>>> mPending;
    };
}

#endif //ASYNCIO_MUTEX_H

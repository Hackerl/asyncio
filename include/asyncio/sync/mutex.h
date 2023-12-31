#ifndef ASYNCIO_MUTEX_H
#define ASYNCIO_MUTEX_H

#include <atomic>
#include <asyncio/future.h>

namespace asyncio::sync {
    class Mutex {
    public:
        zero::async::coroutine::Task<void, std::error_code> lock();
        void unlock();

    private:
        std::atomic<bool> mLocked;
        std::list<Future<void>> mPending;
    };
}

#endif //ASYNCIO_MUTEX_H

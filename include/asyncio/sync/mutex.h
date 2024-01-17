#ifndef ASYNCIO_MUTEX_H
#define ASYNCIO_MUTEX_H

#include <atomic>
#include <asyncio/future.h>

namespace asyncio::sync {
    class Mutex {
    public:
        zero::async::coroutine::Task<void, std::error_code>
        lock(std::optional<std::chrono::milliseconds> ms = std::nullopt);

        void unlock();

        [[nodiscard]] bool locked() const;

    private:
        std::atomic<bool> mLocked;
        std::list<FuturePtr<void>> mPending;
    };
}

#endif //ASYNCIO_MUTEX_H

#ifndef ASYNCIO_SYNC_EVENT_H
#define ASYNCIO_SYNC_EVENT_H

#include <atomic>
#include <asyncio/promise.h>

namespace asyncio::sync {
    class Event {
    public:
        zero::async::coroutine::Task<void, std::error_code> wait();

        void set();
        void reset();

        [[nodiscard]] bool isSet() const;

    private:
        std::atomic<bool> mValue;
        std::list<std::shared_ptr<Promise<void, std::error_code>>> mPending;
    };
}

#endif //ASYNCIO_SYNC_EVENT_H

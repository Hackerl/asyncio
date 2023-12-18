#ifndef ASYNCIO_SYNC_EVENT_H
#define ASYNCIO_SYNC_EVENT_H

#include <atomic>
#include <asyncio/future.h>

namespace asyncio::sync {
    class Event {
    public:
        zero::async::coroutine::Task<void, std::error_code>
        wait(std::optional<std::chrono::milliseconds> ms = std::nullopt);

        void set();
        void clear();

        [[nodiscard]] bool isSet() const;

    private:
        std::atomic<bool> mValue;
        std::list<Future<void>> mPending;
    };
}

#endif //ASYNCIO_SYNC_EVENT_H

#ifndef ASYNCIO_SYNC_EVENT_H
#define ASYNCIO_SYNC_EVENT_H

#include <asyncio/task.h>

namespace asyncio::sync {
    class Event {
    public:
        Event() = default;

        Event(const Event &rhs) = delete;
        Event(Event &&rhs) = default;

        Event &operator=(const Event &rhs) = delete;
        Event &operator=(Event &&rhs) noexcept = default;

        task::Task<void, std::error_code> wait();

        void set();
        void reset();

        [[nodiscard]] bool isSet() const;

    private:
        bool mValue{false};
        std::list<std::shared_ptr<Promise<void, std::error_code>>> mPending;
    };
}

#endif //ASYNCIO_SYNC_EVENT_H

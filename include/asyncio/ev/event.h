#ifndef ASYNCIO_EVENT_H
#define ASYNCIO_EVENT_H

#include <optional>
#include <event.h>
#include <asyncio/io.h>
#include <asyncio/promise.h>
#include <zero/async/coroutine.h>

namespace asyncio::ev {
    enum What : short {
        TIMEOUT = EV_TIMEOUT,
        READ = EV_READ,
        WRITE = EV_WRITE,
        CLOSED = EV_CLOSED
    };

    class Event {
    public:
        explicit Event(std::unique_ptr<event, decltype(event_free) *> event);
        Event(Event &&rhs) noexcept;
        Event &operator=(Event &&rhs) noexcept;
        ~Event();

        static tl::expected<Event, std::error_code> make(FileDescriptor fd, short events);

        [[nodiscard]] FileDescriptor fd() const;

        bool cancel();
        [[nodiscard]] bool pending() const;

        zero::async::coroutine::Task<short, std::error_code>
        on(std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    private:
        std::unique_ptr<event, decltype(event_free) *> mEvent;
        std::optional<Promise<short, std::error_code>> mPromise;
    };
}

#endif //ASYNCIO_EVENT_H

#ifndef ASYNCIO_EVENT_H
#define ASYNCIO_EVENT_H

#include <optional>
#include <event.h>
#include <asyncio/io.h>
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
        explicit Event(event *e);
        Event(Event &&rhs) noexcept;
        ~Event();

        [[nodiscard]] FileDescriptor fd() const;

        bool cancel();
        [[nodiscard]] bool pending() const;

        zero::async::coroutine::Task<short, std::error_code>
        on(std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    private:
        std::unique_ptr<event, decltype(event_free) *> mEvent;
        std::optional<zero::async::promise::Promise<short, std::error_code>> mPromise;
    };

    tl::expected<Event, std::error_code> makeEvent(FileDescriptor fd, short events);
}

#endif //ASYNCIO_EVENT_H

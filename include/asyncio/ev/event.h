#ifndef ASYNCIO_EVENT_H
#define ASYNCIO_EVENT_H

#include <chrono>
#include <optional>
#include <event.h>
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
        explicit Event(evutil_socket_t fd);

    public:
        Event(const Event &) = delete;
        ~Event();

    public:
        Event &operator=(const Event &) = delete;

    public:
        evutil_socket_t fd();

    public:
        bool cancel();
        bool pending();
        void trigger(short events);

    public:
        zero::async::coroutine::Task<short, std::error_code> on(
                short events,
                std::optional<std::chrono::milliseconds> timeout = std::nullopt
        );

    private:
        event *mEvent;
        std::unique_ptr<zero::async::promise::Promise<short, std::error_code>> mPromise;
    };
}

#endif //ASYNCIO_EVENT_H

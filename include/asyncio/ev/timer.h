#ifndef ASYNCIO_TIMER_H
#define ASYNCIO_TIMER_H

#include <chrono>
#include <optional>
#include <event.h>
#include <asyncio/promise.h>
#include <zero/async/coroutine.h>

namespace asyncio::ev {
    class Timer {
    public:
        explicit Timer(std::unique_ptr<event, decltype(event_free) *> event);
        Timer(Timer &&rhs) noexcept;
        Timer &operator=(Timer &&rhs) noexcept;
        ~Timer();

        static tl::expected<Timer, std::error_code> make();

        bool cancel();
        [[nodiscard]] bool pending() const;

        zero::async::coroutine::Task<void, std::error_code> after(std::chrono::milliseconds delay);

    private:
        std::unique_ptr<event, decltype(event_free) *> mEvent;
        std::optional<Promise<void, std::error_code>> mPromise;
    };
}

#endif //ASYNCIO_TIMER_H

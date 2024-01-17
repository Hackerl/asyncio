#ifndef ASYNCIO_TIMER_H
#define ASYNCIO_TIMER_H

#include <chrono>
#include <optional>
#include <event.h>
#include <zero/async/coroutine.h>

namespace asyncio::ev {
    class Timer {
    public:
        explicit Timer(event *e);
        Timer(Timer &&rhs) noexcept;
        ~Timer();

        bool cancel();
        [[nodiscard]] bool pending() const;

        zero::async::coroutine::Task<void, std::error_code> after(std::chrono::milliseconds delay);

    private:
        std::unique_ptr<event, decltype(event_free) *> mEvent;
        zero::async::promise::PromisePtr<void, std::error_code> mPromise;
    };

    tl::expected<Timer, std::error_code> makeTimer();
}

#endif //ASYNCIO_TIMER_H

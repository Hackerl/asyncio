#ifndef ASYNCIO_TIMER_H
#define ASYNCIO_TIMER_H

#include "event.h"
#include <chrono>
#include <zero/interface.h>
#include <zero/async/coroutine.h>

namespace asyncio::ev {
    class Timer : public Notifier<void> {
    public:
        explicit Timer(event *e);

    public:
        zero::async::coroutine::Task<void, std::error_code> after(std::chrono::milliseconds delay);
    };

    tl::expected<Timer, std::error_code> makeTimer();
}

#endif //ASYNCIO_TIMER_H

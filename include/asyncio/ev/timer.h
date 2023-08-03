#ifndef ASYNCIO_TIMER_H
#define ASYNCIO_TIMER_H

#include <event.h>
#include <chrono>
#include <zero/interface.h>
#include <zero/async/coroutine.h>

namespace asyncio::ev {
    class Timer : public zero::Interface {
    public:
        Timer();
        Timer(const Timer &) = delete;
        ~Timer() override;

    public:
        Timer &operator=(const Timer &) = delete;

    public:
        bool cancel();
        bool pending();

    public:
        zero::async::coroutine::Task<void, std::error_code> setTimeout(std::chrono::milliseconds delay);

    private:
        event *mEvent;
        std::unique_ptr<zero::async::promise::Promise<void, std::error_code>> mPromise;
    };
}

#endif //ASYNCIO_TIMER_H

#ifndef ASYNCIO_SIGNAL_H
#define ASYNCIO_SIGNAL_H

#include <event.h>
#include <zero/interface.h>
#include <zero/async/coroutine.h>

namespace asyncio::ev {
    class Signal : public zero::Interface {
    public:
        explicit Signal(int sig);
        Signal(const Signal &) = delete;
        ~Signal() override;

    public:
        Signal &operator=(const Signal &) = delete;

    public:
        int sig();

    public:
        bool cancel();
        bool pending();

    public:
        zero::async::coroutine::Task<void, std::error_code> on();

    private:
        event *mEvent;
        std::unique_ptr<zero::async::promise::Promise<void, std::error_code>> mPromise;
    };
}

#endif //ASYNCIO_SIGNAL_H

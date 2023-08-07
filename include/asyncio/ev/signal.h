#ifndef ASYNCIO_SIGNAL_H
#define ASYNCIO_SIGNAL_H

#include "event.h"
#include <zero/interface.h>
#include <zero/async/coroutine.h>

namespace asyncio::ev {
    class Signal : public Notifier<void> {
    public:
        explicit Signal(std::unique_ptr<event, void (*)(event *)> event);

    public:
        int sig();

    public:
        zero::async::coroutine::Task<void, std::error_code> on();
    };

    tl::expected<Signal, std::error_code> makeSignal(int sig);
}

#endif //ASYNCIO_SIGNAL_H

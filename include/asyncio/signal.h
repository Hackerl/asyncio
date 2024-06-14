#ifndef ASYNCIO_SIGNAL_H
#define ASYNCIO_SIGNAL_H

#include "uv.h"
#include <zero/async/coroutine.h>

namespace asyncio {
    class Signal {
    public:
        explicit Signal(uv::Handle<uv_signal_t> signal);
        static std::expected<Signal, std::error_code> make();

        zero::async::coroutine::Task<int, std::error_code> on(int sig);

    private:
        uv::Handle<uv_signal_t> mSignal;
    };
}

#endif //ASYNCIO_SIGNAL_H

#ifndef ASYNCIO_SIGNAL_H
#define ASYNCIO_SIGNAL_H

#include "task.h"

namespace asyncio {
    class Signal {
    public:
        explicit Signal(uv::Handle<uv_signal_t> signal);
        static Signal make();

        task::Task<int, std::error_code> on(int sig);

    private:
        uv::Handle<uv_signal_t> mSignal;
    };
}

#endif //ASYNCIO_SIGNAL_H

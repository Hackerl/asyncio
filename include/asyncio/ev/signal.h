#ifndef ASYNCIO_SIGNAL_H
#define ASYNCIO_SIGNAL_H

#include <asyncio/channel.h>

namespace asyncio::ev {
    class Signal {
    public:
        Signal(event *e, std::size_t capacity);
        Signal(Signal &&rhs) noexcept;
        ~Signal();

        [[nodiscard]] int sig() const;
        [[nodiscard]] zero::async::coroutine::Task<int, std::error_code> on() const;

    private:
        std::unique_ptr<Channel<int>> mChannel;
        std::unique_ptr<event, decltype(event_free) *> mEvent;
    };

    tl::expected<Signal, std::error_code> makeSignal(int sig, std::size_t capacity = 64);
}

#endif //ASYNCIO_SIGNAL_H

#ifndef ASYNCIO_SIGNAL_H
#define ASYNCIO_SIGNAL_H

#include <asyncio/channel.h>

namespace asyncio::ev {
    class Signal {
    public:
        Signal(std::unique_ptr<event, decltype(event_free) *> event, std::size_t capacity);
        Signal(Signal &&rhs) noexcept;
        Signal &operator=(Signal &&rhs) noexcept;
        ~Signal();

        static tl::expected<Signal, std::error_code> make(int sig, std::size_t capacity = 64);

        [[nodiscard]] int sig() const;
        zero::async::coroutine::Task<int, std::error_code> on();

    private:
        Channel<int> mChannel;
        std::unique_ptr<event, decltype(event_free) *> mEvent;
    };
}

#endif //ASYNCIO_SIGNAL_H

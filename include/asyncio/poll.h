#ifndef ASYNCIO_POLL_H
#define ASYNCIO_POLL_H

#include "uv.h"
#include <zero/async/coroutine.h>

namespace asyncio {
    class Poll {
    public:
        enum Event {
            READABLE = UV_READABLE,
            WRITABLE = UV_WRITABLE,
            DISCONNECT = UV_DISCONNECT,
            PRIORITIZED = UV_PRIORITIZED
        };

        explicit Poll(uv::Handle<uv_poll_t> poll);
        static std::expected<Poll, std::error_code> make(int fd);
#ifdef _WIN32
        static std::expected<Poll, std::error_code> make(SOCKET socket);
#endif

        zero::async::coroutine::Task<int, std::error_code> on(int events);

    private:
        uv::Handle<uv_poll_t> mPoll;
    };
}

#endif //ASYNCIO_POLL_H

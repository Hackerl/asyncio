#ifndef ASYNCIO_EVENT_LOOP_H
#define ASYNCIO_EVENT_LOOP_H

#include "worker.h"
#include <queue>
#include <event.h>
#include <zero/async/coroutine.h>

namespace asyncio {
    class EventLoop {
    public:
        EventLoop(event_base *base, evdns_base *dnsBase, size_t maxWorkers);
        EventLoop(const EventLoop &) = delete;
        ~EventLoop();

    public:
        EventLoop &operator=(const EventLoop &) = delete;

    public:
        event_base *base();
        evdns_base *dnsBase();

    public:
        bool addNameserver(const char *ip);

    public:
        void dispatch();
        void loopBreak();
        void loopExit(std::optional<std::chrono::milliseconds> ms = std::nullopt);

    public:
        template<typename F>
        void post(F &&f, std::optional<std::chrono::milliseconds> ms = std::nullopt) {
            std::optional<timeval> tv;

            if (ms)
                tv = {
                        (time_t) (ms->count() / 1000),
                        (suseconds_t) ((ms->count() % 1000) * 1000)
                };

            auto ctx = new std::decay_t<F>(std::forward<F>(f));

            event_base_once(
                    mBase,
                    -1,
                    EV_TIMEOUT,
                    [](evutil_socket_t, short, void *arg) {
                        auto ctx = (std::decay_t<F> *) arg;

                        ctx->operator()();
                        delete ctx;
                    },
                    ctx,
                    tv ? &*tv : nullptr
            );
        }

    private:
        size_t mMaxWorkers;
        event_base *mBase;
        evdns_base *mDnsBase;
        std::queue<std::unique_ptr<Worker>> mWorkers;

        template<typename F>
        friend zero::async::coroutine::Task<
                zero::async::promise::promise_result_t<std::invoke_result_t<F>>,
                std::conditional_t<
                        zero::detail::is_specialization<std::invoke_result_t<F>, tl::expected>,
                        zero::async::promise::promise_reason_t<std::invoke_result_t<F>>,
                        std::exception_ptr
                >
        > toThread(F &&f);
    };

    std::shared_ptr<EventLoop> getEventLoop();
    bool setEventLoop(const std::weak_ptr<EventLoop> &eventLoop);

    tl::expected<std::shared_ptr<EventLoop>, std::error_code> newEventLoop(size_t maxWorkers = 16);
    zero::async::coroutine::Task<void> sleep(std::chrono::milliseconds ms);

    template<typename F>
    tl::expected<void, std::error_code> run(F &&f) {
        auto result = newEventLoop();

        if (!result)
            return tl::unexpected(result.error());

        auto &eventLoop = result.value();
        setEventLoop(eventLoop);

        f().promise.finally([&]() {
            eventLoop->loopExit();
        });

        eventLoop->dispatch();
        return {};
    }
}

#endif //ASYNCIO_EVENT_LOOP_H

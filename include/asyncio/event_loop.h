#ifndef ASYNCIO_EVENT_LOOP_H
#define ASYNCIO_EVENT_LOOP_H

#include <queue>
#include <event.h>
#include <event2/dns.h>
#include <zero/try.h>
#include "worker.h"

namespace asyncio {
    class EventLoop {
    public:
        EventLoop(event_base *base, evdns_base *dnsBase, size_t maxWorkers);

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
                        (decltype(timeval::tv_sec)) (ms->count() / 1000),
                        (decltype(timeval::tv_usec)) ((ms->count() % 1000) * 1000)
                };

            auto ctx = new std::decay_t<F>(std::forward<F>(f));

            event_base_once(
                    mBase.get(),
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
        std::queue<std::unique_ptr<Worker>> mWorkers;
        std::unique_ptr<event_base, decltype(event_base_free) *> mBase;
        std::unique_ptr<evdns_base, void (*)(evdns_base *)> mDnsBase;

        template<typename F>
        friend zero::async::coroutine::Task<typename std::invoke_result_t<F>::value_type, std::error_code>
        toThread(F f);

        template<typename F, typename C>
        friend zero::async::coroutine::Task<typename std::invoke_result_t<F>::value_type, std::error_code>
        toThread(F f, C cancel);
    };

    std::shared_ptr<EventLoop> getEventLoop();
    bool setEventLoop(const std::weak_ptr<EventLoop> &eventLoop);

    tl::expected<EventLoop, std::error_code> createEventLoop(size_t maxWorkers = 16);
    zero::async::coroutine::Task<void, std::error_code> sleep(std::chrono::milliseconds ms);

    template<typename T, typename E>
    zero::async::coroutine::Task<tl::expected<T, E>, std::error_code>
    timeout(zero::async::coroutine::Task<T, E> task, std::chrono::milliseconds ms) {
        if (ms == std::chrono::milliseconds::zero()) {
            co_await task;
            co_return tl::expected<tl::expected<T, E>, std::error_code>{tl::in_place, std::move(task.result())};
        }

        auto timer = sleep(ms);

        if constexpr (std::is_void_v<T>) {
            task.promise().then([=]() mutable {
                timer.cancel();
            }, [=](const E &reason) mutable {
                timer.cancel();
            });
        } else {
            task.promise().then([=](const T &) mutable {
                timer.cancel();
            }, [=](const E &reason) mutable {
                timer.cancel();
            });
        }

        auto result = co_await timer;

        if (result) {
            task.cancel();
            co_return tl::unexpected(make_error_code(std::errc::timed_out));
        }

        if (!task.done()) {
            task.cancel();
            co_return tl::unexpected(result.error());
        }

        co_return tl::expected<tl::expected<T, E>, std::error_code>{tl::in_place, std::move(task.result())};
    }

    template<typename F>
    tl::expected<void, std::error_code> run(F &&f) {
        auto eventLoop = TRY(createEventLoop().transform([](EventLoop &&eventLoop) {
            return std::make_shared<EventLoop>(std::move(eventLoop));
        }));

        setEventLoop(*eventLoop);

        f().promise().finally([&]() {
            eventLoop.value()->loopExit();
        });

        eventLoop.value()->dispatch();
        return {};
    }
}

#endif //ASYNCIO_EVENT_LOOP_H

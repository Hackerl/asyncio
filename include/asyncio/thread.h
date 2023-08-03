#ifndef ASYNCIO_THREAD_H
#define ASYNCIO_THREAD_H

#include "ev/event.h"
#include "event_loop.h"

namespace asyncio {
    template<typename F>
    zero::async::coroutine::Task<
            zero::async::promise::promise_result_t<std::invoke_result_t<F>>,
            std::conditional_t<
                    zero::detail::is_specialization<std::invoke_result_t<F>, tl::expected>,
                    zero::async::promise::promise_reason_t<std::invoke_result_t<F>>,
                    std::exception_ptr
            >
    > toThread(F &&f) {
        using T = std::invoke_result_t<F>;

        auto eventLoop = getEventLoop();

        std::shared_ptr<ev::Event> event = std::make_shared<ev::Event>(-1);
        std::unique_ptr<Worker> worker;

        if (eventLoop->mWorkers.empty()) {
            worker = std::make_unique<Worker>();
        } else {
            worker = std::move(eventLoop->mWorkers.front());
            eventLoop->mWorkers.pop();
        }

        if constexpr (std::is_void_v<T>) {
            auto task = event->on(ev::What::READ);

            worker->execute([=, f = std::forward<F>(f)]() {
                f();
                event->trigger(ev::What::READ);
            });

            co_await task;

            if (eventLoop->mWorkers.size() < eventLoop->mMaxWorkers)
                eventLoop->mWorkers.push(std::move(worker));

            co_return;
        } else {
            T result;
            auto task = event->on(ev::What::READ);

            worker->execute([=, &result, f = std::forward<F>(f)]() {
                result = f();
                event->trigger(ev::What::READ);
            });

            co_await task;

            if (eventLoop->mWorkers.size() < eventLoop->mMaxWorkers)
                eventLoop->mWorkers.push(std::move(worker));

            co_return result;
        }
    }
}

#endif //ASYNCIO_THREAD_H

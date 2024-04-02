#ifndef ASYNCIO_THREAD_H
#define ASYNCIO_THREAD_H

#include "promise.h"

namespace asyncio {
    template<typename F>
    zero::async::coroutine::Task<typename std::invoke_result_t<F>::value_type, std::error_code>
    toThread(F f) {
        using T = std::invoke_result_t<F>;

        const auto eventLoop = getEventLoop();
        Promise<void, std::error_code> promise(eventLoop);

        std::unique_ptr<Worker> worker;

        if (eventLoop->mWorkers.empty()) {
            worker = std::make_unique<Worker>();
        }
        else {
            worker = std::move(eventLoop->mWorkers.front());
            eventLoop->mWorkers.pop();
        }

        T result;

        worker->execute([&, f = std::move(f)] {
            result = f();
            promise.resolve();
        });

        co_await promise.getFuture();

        if (eventLoop->mWorkers.size() < eventLoop->mMaxWorkers)
            eventLoop->mWorkers.push(std::move(worker));

        co_return result;
    }

    template<typename F, typename C>
    zero::async::coroutine::Task<typename std::invoke_result_t<F>::value_type, std::error_code>
    toThread(F f, C cancel) {
        using T = std::invoke_result_t<F>;

        const auto eventLoop = getEventLoop();
        Promise<void, std::error_code> promise(eventLoop);

        std::unique_ptr<Worker> worker;

        if (eventLoop->mWorkers.empty()) {
            worker = std::make_unique<Worker>();
        }
        else {
            worker = std::move(eventLoop->mWorkers.front());
            eventLoop->mWorkers.pop();
        }

        T result;

        worker->execute([&, f = std::move(f)] {
            result = f();
            promise.resolve();
        });

        co_await zero::async::coroutine::Cancellable{
            promise.getFuture(),
            [handle = worker->mThread.native_handle(), cancel = std::move(cancel)] {
                return cancel(handle);
            }
        };

        if (eventLoop->mWorkers.size() < eventLoop->mMaxWorkers)
            eventLoop->mWorkers.push(std::move(worker));

        co_return result;
    }
}

#endif //ASYNCIO_THREAD_H

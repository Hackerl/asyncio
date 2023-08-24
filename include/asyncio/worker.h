#ifndef ASYNCIO_WORKER_H
#define ASYNCIO_WORKER_H

#include <mutex>
#include <thread>
#include <functional>
#include <condition_variable>
#include <zero/atomic/event.h>
#include <zero/async/coroutine.h>

namespace asyncio {
    class Worker {
    public:
        Worker();
        ~Worker();

    public:
        template<typename F>
        void execute(F &&f) {
            std::lock_guard<std::mutex> guard(mMutex);

            mTask = std::forward<F>(f);
            mCond.notify_one();
        }

    private:
        void work();

    private:
        bool mExit;
        std::mutex mMutex;
        std::function<void()> mTask;
        std::condition_variable mCond;
        std::thread mThread;

        template<typename F, typename C>
        friend zero::async::coroutine::Task<
                zero::async::promise::promise_result_t<std::invoke_result_t<F>>,
                std::error_code
        >
        toThread(F &&f, C &&cancel);
    };
}

#endif //ASYNCIO_WORKER_H

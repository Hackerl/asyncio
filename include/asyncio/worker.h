#ifndef ASYNCIO_WORKER_H
#define ASYNCIO_WORKER_H

#include <thread>
#include <functional>
#include <condition_variable>
#include <zero/async/coroutine.h>

namespace asyncio {
    class Worker {
    public:
        Worker();
        ~Worker();

    private:
        void work();

    public:
        std::thread::native_handle_type handle();

        template<typename F>
        void execute(F &&f) {
            std::lock_guard guard(mMutex);

            mTask = std::forward<F>(f);
            mCond.notify_one();
        }

    private:
        bool mExit;
        std::mutex mMutex;
        std::function<void()> mTask;
        std::condition_variable mCond;
        std::thread mThread;
    };
}

#endif //ASYNCIO_WORKER_H

#ifndef ASYNCIO_WORKER_H
#define ASYNCIO_WORKER_H

#include <mutex>
#include <thread>
#include <functional>
#include <condition_variable>
#include <zero/atomic/event.h>

namespace asyncio {
    class Worker {
    public:
        Worker();
        Worker(const Worker &) = delete;

    public:
        ~Worker();

    public:
        Worker &operator=(const Worker &) = delete;

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
    };
}

#endif //ASYNCIO_WORKER_H

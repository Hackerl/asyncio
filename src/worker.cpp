#include <asyncio/worker.h>

asyncio::Worker::Worker() : mExit(false), mThread(&Worker::work, this) {

}

asyncio::Worker::~Worker() {
    {
        std::lock_guard<std::mutex> guard(mMutex);
        mExit = true;
    }

    mCond.notify_one();
    mThread.join();
}

void asyncio::Worker::work() {
    while (true) {
        std::unique_lock<std::mutex> lock(mMutex);

        if (mExit)
            break;

        if (!mTask) {
            mCond.wait(lock);
            continue;
        }

        std::function<void()> task = std::move(mTask);
        task();
    }
}
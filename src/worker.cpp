#include <asyncio/worker.h>

asyncio::Worker::Worker() : mExit(false), mThread(&Worker::work, this) {
}

asyncio::Worker::~Worker() {
    {
        std::lock_guard guard(mMutex);
        mExit = true;
    }

    mCond.notify_one();
    mThread.join();
}

void asyncio::Worker::work() {
    while (true) {
        std::unique_lock lock(mMutex);

        if (mExit)
            break;

        if (!mTask) {
            mCond.wait(lock);
            continue;
        }

        std::exchange(mTask, nullptr)();
    }
}

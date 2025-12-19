#include <asyncio/sync/mutex.h>

void asyncio::sync::Mutex::wakeup() const {
    if (mPending.empty())
        return;

    if (const auto &promise = mPending.front(); !promise->isFulfilled())
        promise->resolve();
}

asyncio::task::Task<void, std::error_code> asyncio::sync::Mutex::lock() {
    if (!mLocked && mPending.empty()) {
        mLocked = true;
        co_return {};
    }

    const auto promise = std::make_shared<Promise<void, std::error_code>>();
    mPending.push_back(promise);

    const auto result = co_await task::CancellableFuture{
        promise->getFuture(),
        [=]() -> std::expected<void, std::error_code> {
            if (promise->isFulfilled())
                return std::unexpected{task::Error::CANCELLATION_TOO_LATE};

            promise->reject(task::Error::CANCELLED);
            return {};
        }
    };

    mPending.remove(promise);

    if (!result) {
        if (!mLocked)
            wakeup();

        co_return std::unexpected{result.error()};
    }

    assert(!mLocked);
    mLocked = true;
    co_return {};
}

void asyncio::sync::Mutex::unlock() {
    assert(mLocked);
    mLocked = false;
    wakeup();
}

bool asyncio::sync::Mutex::locked() const {
    return mLocked;
}

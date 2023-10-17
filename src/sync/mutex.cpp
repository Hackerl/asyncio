#include <asyncio/sync/mutex.h>

zero::async::coroutine::Task<void, std::error_code> asyncio::sync::Mutex::lock() {
    if (!mLocked) {
        mLocked = true;
        co_return tl::expected<void, std::error_code>{};
    }

    Future<void> future;
    mPending.push_back(future);

    auto result = co_await future.get();

    if (!result) {
        if (mPending.remove(future) == 0 && !mPending.empty()) {
            mPending.front().set();
            mPending.pop_front();
        }

        co_return tl::unexpected(result.error());
    }

    assert(!mLocked);
    mLocked = true;
    co_return tl::expected<void, std::error_code>{};
}

void asyncio::sync::Mutex::unlock() {
    assert(mLocked);
    mLocked = false;

    if (mPending.empty())
        return;

    mPending.front().set();
    mPending.pop_front();
}

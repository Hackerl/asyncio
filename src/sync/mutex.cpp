#include <asyncio/sync/mutex.h>

zero::async::coroutine::Task<void, std::error_code>
asyncio::sync::Mutex::lock(const std::optional<std::chrono::milliseconds> ms) {
    if (!mLocked) {
        mLocked = true;
        co_return tl::expected<void, std::error_code>{};
    }

    Future<void> future;
    mPending.push_back(future);

    if (const auto result = co_await future.get(ms); !result) {
        if (mPending.remove(future) == 0 && !mPending.empty()) {
            assert(!mLocked);
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

bool asyncio::sync::Mutex::locked() const {
    return mLocked;
}

#include <asyncio/sync/mutex.h>

void asyncio::sync::Mutex::wakeup() const {
    if (mPending.empty())
        return;

    if (const auto &promise = mPending.front(); !promise->isFulfilled())
        promise->resolve();
}

zero::async::coroutine::Task<void, std::error_code>
asyncio::sync::Mutex::lock(const std::optional<std::chrono::milliseconds> timeout) {
    if (!mLocked && mPending.empty()) {
        mLocked = true;
        co_return tl::expected<void, std::error_code>{};
    }

    const auto promise = std::make_shared<Promise<void, std::error_code>>();
    mPending.push_back(promise);

    const auto result = co_await asyncio::timeout(
        from(zero::async::coroutine::Cancellable{
            promise->getFuture(),
            [=]() -> tl::expected<void, std::error_code> {
                if (promise->isFulfilled())
                    return tl::unexpected(make_error_code(std::errc::operation_not_supported));

                promise->reject(make_error_code(std::errc::operation_canceled));
                return {};
            }
        }),
        timeout.value_or(std::chrono::milliseconds{0})
    ).andThen([](const auto &res) -> tl::expected<void, std::error_code> {
        if (!res)
            return tl::unexpected(res.error());

        return {};
    });

    mPending.remove(promise);

    if (!result) {
        if (!mLocked)
            wakeup();

        co_return tl::unexpected(result.error());
    }

    assert(!mLocked);
    mLocked = true;
    co_return tl::expected<void, std::error_code>{};
}

void asyncio::sync::Mutex::unlock() {
    assert(mLocked);
    mLocked = false;
    wakeup();
}

bool asyncio::sync::Mutex::locked() const {
    return mLocked;
}

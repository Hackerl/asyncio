#include <asyncio/sync/condition.h>

zero::async::coroutine::Task<void, std::error_code>
asyncio::sync::Condition::wait(Mutex &mutex, const std::optional<std::chrono::milliseconds> timeout) {
    const int counter = mCounter;

    assert(mutex.locked());
    mutex.unlock();

    const auto promise = std::make_shared<Promise<void, std::error_code>>();
    mPending.push_back(promise);

    const auto result = co_await asyncio::timeout(
        from(zero::async::coroutine::Cancellable{
            promise->getFuture(),
            [=, this]() -> tl::expected<void, std::error_code> {
                if (mPending.remove(promise) == 0)
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

    while (true) {
        if (co_await mutex.lock())
            break;
    }

    if (!result) {
        if (counter != mCounter)
            notify();

        co_return tl::unexpected(result.error());
    }

    co_return tl::expected<void, std::error_code>{};
}

void asyncio::sync::Condition::notify() {
    ++mCounter;

    if (mPending.empty())
        return;

    mPending.front()->resolve();
    mPending.pop_front();
}

void asyncio::sync::Condition::broadcast() {
    ++mCounter;

    if (mPending.empty())
        return;

    for (const auto &promise: std::exchange(mPending, {}))
        promise->resolve();
}

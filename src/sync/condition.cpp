#include <asyncio/sync/condition.h>

zero::async::coroutine::Task<void, std::error_code> asyncio::sync::Condition::wait(Mutex &mutex) {
    const int counter = mCounter;

    assert(mutex.locked());
    mutex.unlock();

    const auto promise = std::make_shared<Promise<void, std::error_code>>();
    mPending.push_back(promise);

    const auto result = co_await zero::async::coroutine::Cancellable{
        promise->getFuture(),
        [=, this]() -> std::expected<void, std::error_code> {
            if (mPending.remove(promise) == 0)
                return std::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

            promise->reject(zero::async::coroutine::Error::CANCELLED);
            return {};
        }
    };

    co_await zero::async::coroutine::lock;

    while (true) {
        if (co_await mutex.lock())
            break;
    }

    co_await zero::async::coroutine::unlock;

    if (!result) {
        if (counter != mCounter)
            notify();

        co_return std::unexpected(result.error());
    }

    co_return {};
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

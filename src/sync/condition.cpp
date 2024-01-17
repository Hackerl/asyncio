#include <asyncio/sync/condition.h>

zero::async::coroutine::Task<void, std::error_code>
asyncio::sync::Condition::wait(Mutex &mutex, const std::optional<std::chrono::milliseconds> ms) {
    assert(mutex.locked());
    mutex.unlock();

    const auto future = makeFuture<void>();
    mPending.push_back(future);

    if (const auto result = co_await future->get(ms); !result) {
        mPending.remove(future);

        while (true) {
            if (co_await mutex.lock())
                break;
        }

        co_return tl::unexpected(result.error());
    }

    while (true) {
        if (co_await mutex.lock())
            break;
    }

    co_return tl::expected<void, std::error_code>{};
}

void asyncio::sync::Condition::notify() {
    if (mPending.empty())
        return;

    mPending.front()->set();
    mPending.pop_front();
}

void asyncio::sync::Condition::broadcast() {
    for (const auto &future: std::exchange(mPending, {}))
        future->set();
}

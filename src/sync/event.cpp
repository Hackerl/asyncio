#include <asyncio/sync/event.h>

zero::async::coroutine::Task<void, std::error_code>
asyncio::sync::Event::wait(const std::optional<std::chrono::milliseconds> ms) {
    if (mValue)
        co_return tl::expected<void, std::error_code>{};

    const auto future = makeFuture<void>();
    mPending.push_back(future);

    if (const auto result = co_await future->get(ms); !result) {
        mPending.remove(future);
        co_return tl::unexpected(result.error());
    }

    co_return tl::expected<void, std::error_code>{};
}

void asyncio::sync::Event::set() {
    if (mValue)
        return;

    mValue = true;

    for (const auto &future: std::exchange(mPending, {}))
        future->set();
}

void asyncio::sync::Event::clear() {
    mValue = false;
}

bool asyncio::sync::Event::isSet() const {
    return mValue;
}

#include <asyncio/sync/event.h>

asyncio::task::Task<void, std::error_code> asyncio::sync::Event::wait() {
    if (mValue)
        co_return {};

    const auto promise = std::make_shared<Promise<void, std::error_code>>();
    mPending.push_back(promise);

    co_return co_await task::Cancellable{
        promise->getFuture(),
        [=, this]() -> std::expected<void, std::error_code> {
            if (mPending.remove(promise) == 0)
                return std::unexpected{task::Error::WILL_BE_DONE};

            promise->reject(task::Error::CANCELLED);
            return {};
        }
    };
}

void asyncio::sync::Event::set() {
    if (mValue)
        return;

    mValue = true;

    for (const auto &promise: std::exchange(mPending, {}))
        promise->resolve();
}

void asyncio::sync::Event::reset() {
    mValue = false;
}

bool asyncio::sync::Event::isSet() const {
    return mValue;
}

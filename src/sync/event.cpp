#include <asyncio/sync/event.h>

zero::async::coroutine::Task<void, std::error_code>
asyncio::sync::Event::wait(const std::optional<std::chrono::milliseconds> timeout) {
    if (mValue)
        co_return tl::expected<void, std::error_code>{};

    const auto promise = std::make_shared<Promise<void, std::error_code>>();
    mPending.push_back(promise);

    co_return co_await asyncio::timeout(
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
    ).andThen([](const auto &result) -> tl::expected<void, std::error_code> {
        if (!result)
            return tl::unexpected(result.error());

        return {};
    });
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

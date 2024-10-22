#include <asyncio/time.h>

asyncio::task::Task<void, std::error_code> asyncio::sleep(const std::chrono::milliseconds ms) {
    auto ptr = std::make_unique<uv_timer_t>();

    CO_EXPECT(uv::expected([&] {
        return uv_timer_init(getEventLoop()->raw(), ptr.get());
    }));

    uv::Handle timer{std::move(ptr)};

    Promise<void, std::error_code> promise;
    timer->data = &promise;

    CO_EXPECT(uv::expected([&] {
        return uv_timer_start(
            timer.raw(),
            [](auto *handle) {
                uv_timer_stop(handle);
                static_cast<Promise<void, std::error_code> *>(handle->data)->resolve();
            },
            ms.count(),
            0
        );
    }));

    co_return co_await task::Cancellable{
        promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return std::unexpected{task::Error::WILL_BE_DONE};

            uv_timer_stop(timer.raw());
            promise.reject(task::Error::CANCELLED);
            return {};
        }
    };
}

DEFINE_ERROR_CATEGORY_INSTANCE(asyncio::TimeoutError)

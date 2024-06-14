#include <asyncio/promise.h>
#include <asyncio/time.h>

zero::async::coroutine::Task<void, std::error_code> asyncio::sleep(const std::chrono::milliseconds ms) {
    auto ptr = std::make_unique<uv_timer_t>();

    CO_EXPECT(uv::expected([&] {
        return uv_timer_init(getEventLoop()->raw(), ptr.get());
    }));

    uv::Handle timer(std::move(ptr));

    Promise<void, std::error_code> promise;
    timer->data = &promise;

    CO_EXPECT(uv::expected([&] {
        return uv_timer_start(
            timer.raw(),
            [](uv_timer_t *handle) {
                uv_timer_stop(handle);
                static_cast<Promise<void, std::error_code> *>(handle->data)->resolve();
            },
            ms.count(),
            0
        );
    }));

    co_return co_await zero::async::coroutine::Cancellable{
        promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return std::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

            uv_timer_stop(timer.raw());
            promise.reject(zero::async::coroutine::Error::CANCELLED);
            return {};
        }
    };
}

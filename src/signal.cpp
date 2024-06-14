#include <asyncio/signal.h>
#include <asyncio/promise.h>

asyncio::Signal::Signal(uv::Handle<uv_signal_t> signal) : mSignal(std::move(signal)) {
}

std::expected<asyncio::Signal, std::error_code> asyncio::Signal::make() {
    auto signal = std::make_unique<uv_signal_t>();

    EXPECT(uv::expected([&] {
        return uv_signal_init(getEventLoop()->raw(), signal.get());
    }));

    return Signal{uv::Handle{std::move(signal)}};
}

zero::async::coroutine::Task<int, std::error_code> asyncio::Signal::on(const int sig) {
    Promise<int, std::error_code> promise;
    mSignal->data = &promise;

    CO_EXPECT(uv::expected([&] {
        return uv_signal_start_oneshot(
            mSignal.raw(),
            [](const auto handle, const int s) {
                static_cast<Promise<int, std::error_code> *>(handle->data)->resolve(s);
            },
            sig
        );
    }));

    co_return co_await zero::async::coroutine::Cancellable{
        promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return std::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

            uv_signal_stop(mSignal.raw());
            promise.reject(zero::async::coroutine::Error::CANCELLED);
            return {};
        }
    };
}

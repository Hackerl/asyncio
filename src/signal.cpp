#include <asyncio/signal.h>

asyncio::Signal::Signal(uv::Handle<uv_signal_t> signal) : mSignal{std::move(signal)} {
}

std::expected<asyncio::Signal, std::error_code> asyncio::Signal::make() {
    auto signal = std::make_unique<uv_signal_t>();

    Z_EXPECT(uv::expected([&] {
        return uv_signal_init(getEventLoop()->raw(), signal.get());
    }));

    return Signal{uv::Handle{std::move(signal)}};
}

asyncio::task::Task<int, std::error_code> asyncio::Signal::on(const int sig) {
    Promise<int, std::error_code> promise;
    mSignal->data = &promise;

    Z_CO_EXPECT(uv::expected([&] {
        return uv_signal_start_oneshot(
            mSignal.raw(),
            [](auto *handle, const int s) {
                static_cast<Promise<int, std::error_code> *>(handle->data)->resolve(s);
            },
            sig
        );
    }));

    co_return co_await task::CancellableFuture{
        promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return std::unexpected{task::Error::WILL_BE_DONE};

            uv_signal_stop(mSignal.raw());
            promise.reject(task::Error::CANCELLED);
            return {};
        }
    };
}

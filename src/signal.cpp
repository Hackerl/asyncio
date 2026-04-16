#include <asyncio/signal.h>
#include <asyncio/error.h>

asyncio::Signal::Signal(uv::Handle<uv_signal_t> signal) : mSignal{std::move(signal)} {
}

asyncio::Signal asyncio::Signal::make() {
    auto signal = std::make_unique<uv_signal_t>();

    zero::error::guard(uv::expected([&] {
        return uv_signal_init(getEventLoop()->raw(), signal.get());
    }));

    return Signal{uv::Handle{std::move(signal)}};
}

asyncio::task::Task<int, std::error_code> asyncio::Signal::on(const int sig) {
    Promise<int, std::error_code> promise;
    mSignal->data = &promise;

    co_await error::guard(uv::expected([&] {
        return uv_signal_start_oneshot(
            mSignal.raw(),
            [](auto *handle, const int s) {
                static_cast<Promise<int, std::error_code> *>(handle->data)->resolve(s);
            },
            sig
        );
    }));

    co_return co_await task::Cancellable{
        promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return std::unexpected{task::Error::CancellationTooLate};

            zero::error::guard(uv::expected([&] {
                return uv_signal_stop(mSignal.raw());
            }));

            promise.reject(task::Error::Cancelled);
            return {};
        }
    };
}

#ifndef ASYNCIO_THREAD_H
#define ASYNCIO_THREAD_H

#include "promise.h"

namespace asyncio {
    DEFINE_ERROR_CODE_EX(
        ToThreadError,
        "asyncio::toThread",
        CANCELLED, "request has been cancelled", std::errc::operation_canceled
    )

    template<typename F>
    zero::async::coroutine::Task<std::invoke_result_t<F>, ToThreadError>
    toThread(F f) {
        using T = std::invoke_result_t<F>;

        if constexpr (std::is_void_v<T>) {
            struct Context {
                std::decay_t<F> function;
                Promise<int> promise;
            };

            Context context = {std::move(f)};
            uv_work_t request = {.data = &context};

            const auto result = uv_queue_work(
                getEventLoop()->raw(),
                &request,
                [](uv_work_t *req) {
                    static_cast<Context *>(req->data)->function();
                },
                [](uv_work_t *req, const int status) {
                    static_cast<Context *>(req->data)->promise.resolve(status);
                }
            );
            assert(result == 0);

            if (const int status = *co_await zero::async::coroutine::Cancellable{
                context.promise.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    EXPECT(uv::expected([&] {
                        return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
                    }));
                    return {};
                }
            }; status < 0) {
                assert(status == UV_ECANCELED);
                co_return std::unexpected(ToThreadError::CANCELLED);
            }

            co_return {};
        }
        else {
            struct Context {
                std::decay_t<F> function;
                Promise<int> promise;
                std::optional<T> result;
            };

            Context context = {std::move(f)};
            uv_work_t request = {.data = &context};

            const auto result = uv_queue_work(
                getEventLoop()->raw(),
                &request,
                [](uv_work_t *req) {
                    auto &[function, promise, result] = *static_cast<Context *>(req->data);
                    result.emplace(function());
                },
                [](uv_work_t *req, const int status) {
                    static_cast<Context *>(req->data)->promise.resolve(status);
                }
            );
            assert(result == 0);

            if (const int status = *co_await zero::async::coroutine::Cancellable{
                context.promise.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    EXPECT(uv::expected([&] {
                        return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
                    }));
                    return {};
                }
            }; status < 0) {
                assert(status == UV_ECANCELED);
                co_return std::unexpected(ToThreadError::CANCELLED);
            }

            co_return std::move(*context.result);
        }
    }
}

#endif //ASYNCIO_THREAD_H

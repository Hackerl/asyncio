#ifndef ASYNCIO_THREAD_H
#define ASYNCIO_THREAD_H

#include "task.h"
#include <thread>
#include <zero/defer.h>

namespace asyncio {
    template<typename F>
    task::Task<std::invoke_result_t<F>>
    toThread(F f) {
        using T = std::invoke_result_t<F>;

        Promise<T> promise;

        std::thread thread([&] {
            if constexpr (std::is_void_v<T>) {
                f();
                promise.resolve();
            }
            else {
                promise.resolve(f());
            }
        });
        DEFER(thread.join());

        co_return *co_await promise.getFuture();
    }

    template<typename F, typename C>
        requires std::is_same_v<
            std::invoke_result_t<C, std::thread::native_handle_type>,
            std::expected<void, std::error_code>
        >
    task::Task<std::invoke_result_t<F>>
    toThread(F f, C cancel) {
        using T = std::invoke_result_t<F>;

        Promise<T> promise;

        std::thread thread([&] {
            if constexpr (std::is_void_v<T>) {
                f();
                promise.resolve();
            }
            else {
                promise.resolve(f());
            }
        });
        DEFER(thread.join());

        co_return *co_await task::Cancellable{
            promise.getFuture(),
            [&]() -> std::expected<void, std::error_code> {
                return cancel(thread.native_handle());
            }
        };
    }

    DEFINE_ERROR_CODE_EX(
        ToThreadPoolError,
        "asyncio::toThreadPool",
        CANCELLED, "request has been cancelled", std::errc::operation_canceled
    )

    template<typename F>
    task::Task<std::invoke_result_t<F>, ToThreadPoolError>
    toThreadPool(F f) {
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

            if (const int status = *co_await task::Cancellable{
                context.promise.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    EXPECT(uv::expected([&] {
                        return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
                    }));
                    return {};
                }
            }; status < 0) {
                assert(status == UV_ECANCELED);
                co_return std::unexpected(ToThreadPoolError::CANCELLED);
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

            if (const int status = *co_await task::Cancellable{
                context.promise.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    EXPECT(uv::expected([&] {
                        return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
                    }));
                    return {};
                }
            }; status < 0) {
                assert(status == UV_ECANCELED);
                co_return std::unexpected(ToThreadPoolError::CANCELLED);
            }

            co_return std::move(*context.result);
        }
    }
}

DECLARE_ERROR_CODE(asyncio::ToThreadPoolError)

#endif //ASYNCIO_THREAD_H

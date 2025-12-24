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

        Promise<T, std::exception_ptr> promise;

        std::thread thread{
            [&] {
                try {
                    if constexpr (std::is_void_v<T>) {
                        f();
                        promise.resolve();
                    }
                    else {
                        promise.resolve(f());
                    }
                }
                catch (const std::exception &) {
                    promise.reject(std::current_exception());
                }
            }
        };
        Z_DEFER(thread.join());

        auto result = co_await promise.getFuture();

        if (!result)
            std::rethrow_exception(result.error());

        if constexpr (std::is_void_v<T>)
            co_return;
        else
            co_return *std::move(result);
    }

    template<typename F, typename C>
        requires std::is_same_v<
            std::invoke_result_t<C, std::thread::native_handle_type>,
            std::expected<void, std::error_code>
        >
    task::Task<std::invoke_result_t<F>>
    toThread(F f, C cancel) {
        using T = std::invoke_result_t<F>;

        Promise<T, std::exception_ptr> promise;

        std::thread thread{
            [&] {
                try {
                    if constexpr (std::is_void_v<T>) {
                        f();
                        promise.resolve();
                    }
                    else {
                        promise.resolve(f());
                    }
                }
                catch (const std::exception &) {
                    promise.reject(std::current_exception());
                }
            }
        };
        Z_DEFER(thread.join());

        auto result = co_await task::CancellableFuture{
            promise.getFuture(),
            [&] {
                return cancel(thread.native_handle());
            }
        };

        if (!result)
            std::rethrow_exception(result.error());

        if constexpr (std::is_void_v<T>)
            co_return;
        else
            co_return *std::move(result);
    }

    Z_DEFINE_ERROR_CODE_EX(
        ToThreadPoolError,
        "asyncio::toThreadPool",
        CANCELLED, "Request was cancelled", std::errc::operation_canceled
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

            Context context{std::move(f)};
            uv_work_t request{.data = &context};

            const auto result = uv_queue_work(
                getEventLoop()->raw(),
                &request,
                [](auto *req) {
                    static_cast<Context *>(req->data)->function();
                },
                [](auto *req, const int status) {
                    static_cast<Context *>(req->data)->promise.resolve(status);
                }
            );
            assert(result == 0);

            if (const auto status = *co_await task::CancellableFuture{
                context.promise.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    Z_EXPECT(uv::expected([&] {
                        return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
                    }));
                    return {};
                }
            }; status < 0) {
                assert(status == UV_ECANCELED);
                co_return std::unexpected{ToThreadPoolError::CANCELLED};
            }

            co_return {};
        }
        else {
            struct Context {
                std::decay_t<F> function;
                Promise<int> promise;
                std::optional<T> result;
            };

            Context context{std::move(f)};
            uv_work_t request{.data = &context};

            const auto result = uv_queue_work(
                getEventLoop()->raw(),
                &request,
                [](auto *req) {
                    auto &[function, promise, res] = *static_cast<Context *>(req->data);
                    res.emplace(function());
                },
                [](auto *req, const int status) {
                    static_cast<Context *>(req->data)->promise.resolve(status);
                }
            );
            assert(result == 0);

            if (const auto status = *co_await task::CancellableFuture{
                context.promise.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    Z_EXPECT(uv::expected([&] {
                        return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
                    }));
                    return {};
                }
            }; status < 0) {
                assert(status == UV_ECANCELED);
                co_return std::unexpected{ToThreadPoolError::CANCELLED};
            }

            co_return std::move(*context.result);
        }
    }

    template<typename F, typename C>
        requires std::is_same_v<std::invoke_result_t<C>, std::expected<void, std::error_code>>
    task::Task<std::invoke_result_t<F>, ToThreadPoolError>
    toThreadPool(F f, C cancel) {
        using T = std::invoke_result_t<F>;

        if constexpr (std::is_void_v<T>) {
            struct Context {
                std::decay_t<F> function;
                Promise<int> promise;
            };

            Context context{std::move(f)};
            uv_work_t request{.data = &context};

            const auto result = uv_queue_work(
                getEventLoop()->raw(),
                &request,
                [](auto *req) {
                    static_cast<Context *>(req->data)->function();
                },
                [](auto *req, const int status) {
                    static_cast<Context *>(req->data)->promise.resolve(status);
                }
            );
            assert(result == 0);

            if (const auto status = *co_await task::CancellableFuture{
                context.promise.getFuture(),
                [&] {
                    return uv::expected([&] {
                        return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
                    }).transform([](const auto &) {
                    }).or_else([&](const auto &) {
                        return cancel();
                    });
                }
            }; status < 0) {
                assert(status == UV_ECANCELED);
                co_return std::unexpected{ToThreadPoolError::CANCELLED};
            }

            co_return {};
        }
        else {
            struct Context {
                std::decay_t<F> function;
                Promise<int> promise;
                std::optional<T> result;
            };

            Context context{std::move(f)};
            uv_work_t request{.data = &context};

            const auto result = uv_queue_work(
                getEventLoop()->raw(),
                &request,
                [](auto *req) {
                    auto &[function, promise, res] = *static_cast<Context *>(req->data);
                    res.emplace(function());
                },
                [](auto *req, const int status) {
                    static_cast<Context *>(req->data)->promise.resolve(status);
                }
            );
            assert(result == 0);

            if (const auto status = *co_await task::CancellableFuture{
                context.promise.getFuture(),
                [&] {
                    return uv::expected([&] {
                        return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
                    }).transform([](const auto &) {
                    }).or_else([&](const auto &) {
                        return cancel();
                    });
                }
            }; status < 0) {
                assert(status == UV_ECANCELED);
                co_return std::unexpected{ToThreadPoolError::CANCELLED};
            }

            co_return std::move(*context.result);
        }
    }
}

Z_DECLARE_ERROR_CODE(asyncio::ToThreadPoolError)

#endif //ASYNCIO_THREAD_H

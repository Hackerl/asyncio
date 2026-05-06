#ifndef ASYNCIO_THREAD_H
#define ASYNCIO_THREAD_H

#include "error.h"
#include <thread>
#include <zero/defer.h>

namespace asyncio {
    template<std::invocable F>
    task::Task<std::invoke_result_t<F>>
    toThread(F f) {
        using T = std::invoke_result_t<F>;

        Promise<T> promise;

        std::thread thread{
            [&] {
                try {
                    if constexpr (std::is_void_v<T>) {
                        std::invoke(std::move(f));
                        promise.resolve();
                    }
                    else {
                        promise.resolve(std::invoke(std::move(f)));
                    }
                }
                catch (const std::exception &) {
                    promise.reject(std::current_exception());
                }
            }
        };
        Z_DEFER(thread.join());

        co_return co_await promise.getFuture();
    }

    template<std::invocable F>
    task::Task<std::invoke_result_t<F>>
    toThread(F f, const std::function<std::expected<void, std::error_code>(std::thread::native_handle_type)> cancel) {
        using T = std::invoke_result_t<F>;

        Promise<T> promise;

        std::thread thread{
            [&] {
                try {
                    if constexpr (std::is_void_v<T>) {
                        std::invoke(std::move(f));
                        promise.resolve();
                    }
                    else {
                        promise.resolve(std::invoke(std::move(f)));
                    }
                }
                catch (const std::exception &) {
                    promise.reject(std::current_exception());
                }
            }
        };
        Z_DEFER(thread.join());

        co_return co_await task::Cancellable{
            promise.getFuture(),
            [&] {
                return cancel(thread.native_handle());
            }
        };
    }

    Z_DEFINE_ERROR_CODE_EX(
        ToThreadPoolError,
        "asyncio::toThreadPool",
        Cancelled, "Request was cancelled", std::errc::operation_canceled
    )

    template<std::invocable F>
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

            co_await error::guard(uv::expected([&] {
                return uv_queue_work(
                    getEventLoop()->raw(),
                    &request,
                    [](auto *req) {
                        static_cast<Context *>(req->data)->function();
                    },
                    [](auto *req, const int status) {
                        static_cast<Context *>(req->data)->promise.resolve(status);
                    }
                );
            }));

            if (const auto status = co_await task::Cancellable{
                context.promise.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    Z_EXPECT(uv::expected([&] {
                        return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
                    }));
                    return {};
                }
            }; status < 0) {
                assert(status == UV_ECANCELED);
                co_return std::unexpected{ToThreadPoolError::Cancelled};
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

            co_await error::guard(uv::expected([&] {
                return uv_queue_work(
                    getEventLoop()->raw(),
                    &request,
                    [](auto *req) {
                        auto &[function, promise, result] = *static_cast<Context *>(req->data);
                        result.emplace(function());
                    },
                    [](auto *req, const int status) {
                        static_cast<Context *>(req->data)->promise.resolve(status);
                    }
                );
            }));

            if (const auto status = co_await task::Cancellable{
                context.promise.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    Z_EXPECT(uv::expected([&] {
                        return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
                    }));
                    return {};
                }
            }; status < 0) {
                assert(status == UV_ECANCELED);
                co_return std::unexpected{ToThreadPoolError::Cancelled};
            }

            co_return std::move(*context.result);
        }
    }

    template<std::invocable F>
    task::Task<std::invoke_result_t<F>, ToThreadPoolError>
    toThreadPool(F f, const std::function<std::expected<void, std::error_code>()> cancel) {
        using T = std::invoke_result_t<F>;

        if constexpr (std::is_void_v<T>) {
            struct Context {
                std::decay_t<F> function;
                Promise<int> promise;
            };

            Context context{std::move(f)};
            uv_work_t request{.data = &context};

            co_await error::guard(uv::expected([&] {
                return uv_queue_work(
                    getEventLoop()->raw(),
                    &request,
                    [](auto *req) {
                        static_cast<Context *>(req->data)->function();
                    },
                    [](auto *req, const int status) {
                        static_cast<Context *>(req->data)->promise.resolve(status);
                    }
                );
            }));

            if (const auto status = co_await task::Cancellable{
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
                co_return std::unexpected{ToThreadPoolError::Cancelled};
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

            co_await error::guard(uv::expected([&] {
                return uv_queue_work(
                    getEventLoop()->raw(),
                    &request,
                    [](auto *req) {
                        auto &[function, promise, result] = *static_cast<Context *>(req->data);
                        result.emplace(function());
                    },
                    [](auto *req, const int status) {
                        static_cast<Context *>(req->data)->promise.resolve(status);
                    }
                );
            }));

            if (const auto status = co_await task::Cancellable{
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
                co_return std::unexpected{ToThreadPoolError::Cancelled};
            }

            co_return std::move(*context.result);
        }
    }

    template<std::invocable F>
    task::Task<std::invoke_result_t<F>>
    toThreadPoolCatching(F f) {
        using T = std::invoke_result_t<F>;

        if constexpr (std::is_void_v<T>) {
            struct Context {
                std::decay_t<F> function;
                Promise<int> promise;
                std::exception_ptr exception;
            };

            Context context{std::move(f)};
            uv_work_t request{.data = &context};

            co_await error::guard(uv::expected([&] {
                return uv_queue_work(
                    getEventLoop()->raw(),
                    &request,
                    [](auto *req) {
                        auto &[function, promise, exception] = *static_cast<Context *>(req->data);

                        try {
                            function();
                        }
                        catch (const std::exception &) {
                            exception = std::current_exception();
                        }
                    },
                    [](auto *req, const int status) {
                        static_cast<Context *>(req->data)->promise.resolve(status);
                    }
                );
            }));

            if (const auto status = co_await task::Cancellable{
                context.promise.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    Z_EXPECT(uv::expected([&] {
                        return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
                    }));
                    return {};
                }
            }; status < 0) {
                assert(status == UV_ECANCELED);
                throw co_await error::StacktraceError<std::system_error>::make(ToThreadPoolError::Cancelled);
            }

            if (context.exception)
                std::rethrow_exception(context.exception);
        }
        else {
            struct Context {
                std::decay_t<F> function;
                Promise<int> promise;
                std::optional<T> result;
                std::exception_ptr exception;
            };

            Context context{std::move(f)};
            uv_work_t request{.data = &context};

            co_await error::guard(uv::expected([&] {
                return uv_queue_work(
                    getEventLoop()->raw(),
                    &request,
                    [](auto *req) {
                        auto &[function, promise, result, exception] = *static_cast<Context *>(req->data);

                        try {
                            result.emplace(function());
                        }
                        catch (const std::exception &) {
                            exception = std::current_exception();
                        }
                    },
                    [](auto *req, const int status) {
                        static_cast<Context *>(req->data)->promise.resolve(status);
                    }
                );
            }));

            if (const auto status = co_await task::Cancellable{
                context.promise.getFuture(),
                [&]() -> std::expected<void, std::error_code> {
                    Z_EXPECT(uv::expected([&] {
                        return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
                    }));
                    return {};
                }
            }; status < 0) {
                assert(status == UV_ECANCELED);
                throw co_await error::StacktraceError<std::system_error>::make(ToThreadPoolError::Cancelled);
            }

            if (context.exception)
                std::rethrow_exception(context.exception);

            co_return std::move(*context.result);
        }
    }

    template<std::invocable F>
    task::Task<std::invoke_result_t<F>>
    toThreadPoolCatching(F f, const std::function<std::expected<void, std::error_code>()> cancel) {
        using T = std::invoke_result_t<F>;

        if constexpr (std::is_void_v<T>) {
            struct Context {
                std::decay_t<F> function;
                Promise<int> promise;
                std::exception_ptr exception;
            };

            Context context{std::move(f)};
            uv_work_t request{.data = &context};

            co_await error::guard(uv::expected([&] {
                return uv_queue_work(
                    getEventLoop()->raw(),
                    &request,
                    [](auto *req) {
                        auto &[function, promise, exception] = *static_cast<Context *>(req->data);

                        try {
                            function();
                        }
                        catch (const std::exception &) {
                            exception = std::current_exception();
                        }
                    },
                    [](auto *req, const int status) {
                        static_cast<Context *>(req->data)->promise.resolve(status);
                    }
                );
            }));

            if (const auto status = co_await task::Cancellable{
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
                throw co_await error::StacktraceError<std::system_error>::make(ToThreadPoolError::Cancelled);
            }

            if (context.exception)
                std::rethrow_exception(context.exception);
        }
        else {
            struct Context {
                std::decay_t<F> function;
                Promise<int> promise;
                std::optional<T> result;
                std::exception_ptr exception;
            };

            Context context{std::move(f)};
            uv_work_t request{.data = &context};

            co_await error::guard(uv::expected([&] {
                return uv_queue_work(
                    getEventLoop()->raw(),
                    &request,
                    [](auto *req) {
                        auto &[function, promise, result, exception] = *static_cast<Context *>(req->data);

                        try {
                            result.emplace(function());
                        }
                        catch (const std::exception &) {
                            exception = std::current_exception();
                        }
                    },
                    [](auto *req, const int status) {
                        static_cast<Context *>(req->data)->promise.resolve(status);
                    }
                );
            }));

            if (const auto status = co_await task::Cancellable{
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
                throw co_await error::StacktraceError<std::system_error>::make(ToThreadPoolError::Cancelled);
            }

            if (context.exception)
                std::rethrow_exception(context.exception);

            co_return std::move(*context.result);
        }
    }
}

Z_DECLARE_ERROR_CODE(asyncio::ToThreadPoolError)

#endif //ASYNCIO_THREAD_H

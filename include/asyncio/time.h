#ifndef ASYNCIO_TIME_H
#define ASYNCIO_TIME_H

#include "error.h"

namespace asyncio {
    task::Task<void, std::error_code> sleep(std::chrono::milliseconds ms);

    Z_DEFINE_ERROR_CODE_EX(
        TimeoutError,
        "asyncio::timeout",
        Elapsed, "Deadline has elapsed", std::errc::timed_out
    )

    template<typename T, typename E>
        requires (!std::same_as<E, std::exception_ptr>)
    task::Task<std::expected<T, E>, TimeoutError>
    timeout(task::Task<T, E> task, const std::chrono::milliseconds ms) {
        if (ms == std::chrono::milliseconds::zero())
            co_return std::expected<std::expected<T, E>, TimeoutError>{co_await task};

        auto timer = sleep(ms)
            .andThen([&] {
                return task.cancel();
            });

        auto result = co_await task;

        if (timer.done()) {
            if (!timer.future().result())
                co_return std::expected<std::expected<T, E>, TimeoutError>{std::move(result)};

            co_return std::unexpected{TimeoutError::Elapsed};
        }

        std::ignore = timer.cancel();
        std::ignore = co_await timer;

        co_return std::expected<std::expected<T, E>, TimeoutError>{std::move(result)};
    }

    template<typename T>
    task::Task<T> timeout(task::Task<T> task, const std::chrono::milliseconds ms) {
        if (ms == std::chrono::milliseconds::zero())
            co_return co_await task;

        auto timer = sleep(ms)
            .andThen([&] {
                return task.cancel();
            });

        std::optional<std::expected<T, std::exception_ptr>> result;

        try {
            if constexpr (std::is_void_v<T>) {
                co_await task;
                result.emplace();
            }
            else {
                result.emplace(co_await task);
            }
        }
        catch (const std::exception &) {
            result.emplace(std::unexpected{std::current_exception()});
        }

        if (timer.done()) {
            if (!timer.future().result()) {
                if (!*result)
                    std::rethrow_exception(result->error());

                if constexpr (std::is_void_v<T>)
                    co_return;
                else
                    co_return *std::move(*result);
            }

            throw co_await error::StacktraceError<std::system_error>::make(TimeoutError::Elapsed);
        }

        std::ignore = timer.cancel();
        std::ignore = co_await timer;

        if (!*result)
            std::rethrow_exception(result->error());

        if constexpr (std::is_void_v<T>)
            co_return;
        else
            co_return *std::move(*result);
    }
}

Z_DECLARE_ERROR_CODE(asyncio::TimeoutError)

#endif //ASYNCIO_TIME_H

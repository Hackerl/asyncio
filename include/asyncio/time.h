#ifndef ASYNCIO_TIME_H
#define ASYNCIO_TIME_H

#include "task.h"

namespace asyncio {
    task::Task<void, std::error_code> sleep(std::chrono::milliseconds ms);

    DEFINE_ERROR_CODE_EX(
        TimeoutError,
        "asyncio::timeout",
        ELAPSED, "deadline has elapsed", std::errc::timed_out
    )

    template<typename T, typename E>
        requires (!std::is_same_v<E, std::exception_ptr>)
    task::Task<std::expected<T, E>, TimeoutError>
    timeout(task::Task<T, E> task, const std::chrono::milliseconds ms) {
        if (ms == std::chrono::milliseconds::zero())
            co_return std::expected<std::expected<T, E>, TimeoutError>{co_await task};

        auto timer = sleep(ms);
        auto future = timer.future().then([&] {
            return task.cancel();
        });

        auto result = co_await task;

        if (future.isReady()) {
            if (!future.result())
                co_return std::expected<std::expected<T, E>, TimeoutError>{std::move(result)};

            co_return std::unexpected{TimeoutError::ELAPSED};
        }

        std::ignore = timer.cancel();
        co_await std::move(future);

        co_return std::expected<std::expected<T, E>, TimeoutError>{std::move(result)};
    }
}

DECLARE_ERROR_CODE(asyncio::TimeoutError)

#endif //ASYNCIO_TIME_H

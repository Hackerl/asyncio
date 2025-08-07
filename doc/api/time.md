# Time

This module provides time-related functionality, including sleep and timeout control.

## Function `sleep`

```cpp
task::Task<void, std::error_code> sleep(std::chrono::milliseconds ms);
```

Sleeps for a period of time before resuming execution.

```cpp
co_await asyncio::sleep(1s);
```

## Function `timeout`

```cpp
DEFINE_ERROR_CODE_EX(
    TimeoutError,
    "asyncio::timeout",
    ELAPSED, "deadline has elapsed", std::errc::timed_out
)

template<typename T, typename E>
    requires (!std::is_same_v<E, std::exception_ptr>)
task::Task<std::expected<T, E>, TimeoutError>
timeout(task::Task<T, E> task, const std::chrono::milliseconds ms);
```

Expects a task to complete within a specified time. If the deadline is exceeded, the task will be cancelled and waited for completion.

> `timeout` guarantees that the child task is completed when it returns, so the actual wait time may be much longer than the specified deadline, as not all tasks end immediately after being cancelled.

```cpp
REQUIRE(co_await asyncio::timeout(asyncio::sleep(10ms), 20ms));
REQUIRE_ERROR(co_await asyncio::timeout(asyncio::sleep(20ms), 10ms), asyncio::TimeoutError::ELAPSED);
```

Note that the result returned by `timeout` is a two-layer `std::expected`. The outer `std::expected` indicates whether the child task completed within the deadline, and the inner `std::expected` is the result of the child task itself.

> `timeout` is pessimistic, meaning that even if the deadline is exceeded, if an error occurs while cancelling the child task, it will still return the child task's result rather than `TimeoutError::ELAPSED`.

`timeout` only returns `TimeoutError::ELAPSED` when the deadline is exceeded and the child task is successfully cancelled.

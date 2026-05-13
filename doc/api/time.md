# Time

This module provides time-related functionality, including sleep and timeout control.

## Function `sleep`

```c++
task::Task<void, std::error_code> sleep(std::chrono::milliseconds ms);
```

Sleeps for a period of time before resuming execution.

```c++
co_await asyncio::sleep(1s);
```

## Function `timeout`

```c++
Z_DEFINE_ERROR_CODE_EX(
    TimeoutError,
    "asyncio::timeout",
    Elapsed, "Deadline has elapsed", std::errc::timed_out
)

template<typename T, typename E>
    requires (!std::same_as<E, std::exception_ptr>)
task::Task<std::expected<T, E>, TimeoutError>
timeout(task::Task<T, E> task, const std::chrono::milliseconds ms);

template<typename T>
task::Task<T>
timeout(task::Task<T> task, const std::chrono::milliseconds ms);
```

Expects a task to complete within a given deadline. If the deadline is exceeded, the task is cancelled and awaited.

> `timeout` guarantees that the child task has completed by the time it returns, so the actual wait time may far exceed the specified deadline, since not all tasks terminate immediately after being cancelled.

For error-code-based `Task<T, E>`, `timeout` returns a two-layer `std::expected`: the outer layer indicates whether the child task completed within the deadline, and the inner layer holds the child task's result.

```c++
REQUIRE(co_await asyncio::timeout(asyncio::sleep(10ms), 20ms));
REQUIRE_ERROR(co_await asyncio::timeout(asyncio::sleep(20ms), 10ms), asyncio::TimeoutError::Elapsed);
```

> `timeout` is pessimistic: even if the deadline has elapsed, if an error occurs while cancelling the child task, it will still return the child task's result rather than `TimeoutError::Elapsed`.

`timeout` only returns `TimeoutError::Elapsed` when the deadline has elapsed and the child task was successfully cancelled.

For exception-based `Task<T>`, a timeout throws a `std::system_error` with a call stack attached; exceptions thrown inside the child task also propagate directly, eliminating the need to handle two layers of `std::expected`:

```c++
try {
    co_await asyncio::timeout(someTask(), 10ms);
} catch (const std::system_error &e) {
    if (e.code() == std::errc::timed_out)
        fmt::print(stderr, "Timed out\n");
}
```

> It is not possible to determine whether a timeout error was thrown by `timeout` itself or originated from inside the task — this is a known limitation of the exception-based overload.

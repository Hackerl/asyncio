# Thread

This module provides thread-related functionality for compatibility with synchronous code.

## Function `toThread`

```cpp
template<typename F>
task::Task<std::invoke_result_t<F>>
toThread(F f);

template<typename F, typename C>
requires std::is_same_v<
    std::invoke_result_t<C, std::thread::native_handle_type>,
    std::expected<void, std::error_code>
>
task::Task<std::invoke_result_t<F>>
toThread(F f, C cancel);
```

Runs synchronous blocking code in a new thread and returns the corresponding result upon completion:

```cpp
const auto result = co_await asyncio::toThread([] {
    std::this_thread::sleep_for(50ms);
    return 1024;
});
REQUIRE(result == 1024);
```

Cancellation is not supported by default, but a custom cancellation function can be provided:

```cpp
bool exit{false};

co_await asyncio::toThread(
    [&]() -> std::expected<void, std::error_code> {
        while (!exit) {
            std::this_thread::sleep_for(50ms);
        }
    },
    [&](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
        exit = true;
        return {};
    }
);
```

## Function `toThreadPool`

```cpp
Z_DEFINE_ERROR_CODE_EX(
    ToThreadPoolError,
    "asyncio::toThreadPool",
    CANCELLED, "Request has been cancelled", std::errc::operation_canceled
)

template<typename F>
task::Task<std::invoke_result_t<F>, ToThreadPoolError>
toThreadPool(F f);
```

Runs time-consuming code in a thread pool and returns the corresponding result upon completion:

```cpp
const auto result = co_await asyncio::toThreadPool([] {
    std::this_thread::sleep_for(50ms);
    return 1024;
});
REQUIRE(result == 1024);
```

`toThreadPool` uses `uv_queue_work` internally, managed and scheduled by `libuv`. The upper layer can call `task.cancel()`, and the lower layer will attempt to terminate execution using `uv_cancel`. If the task is still in the queue and hasn't started, cancellation will succeed and return a `ToThreadPoolError::CANCELLED` error.

> Long-blocking code should not be placed in the thread pool, as the number of threads in the pool is limited, which would cause all worker threads to block.

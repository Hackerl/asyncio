# Time

该模块提供时间相关的功能，包括休眠、超时控制等等。

## Function `sleep`

```cpp
task::Task<void, std::error_code> sleep(std::chrono::milliseconds ms);
```

休眠一段时间后恢复执行。

```cpp
co_await asyncio::sleep(1s);
```

## Function `timeout`

```cpp
Z_DEFINE_ERROR_CODE_EX(
    TimeoutError,
    "asyncio::timeout",
    ELAPSED, "deadline has elapsed", std::errc::timed_out
)

template<typename T, typename E>
    requires (!std::is_same_v<E, std::exception_ptr>)
task::Task<std::expected<T, E>, TimeoutError>
timeout(task::Task<T, E> task, const std::chrono::milliseconds ms);
```

期望某个任务在规定时间内完成，如果超过了期限则会取消任务，并等待其完成。

> `timeout` 保证在返回时子任务一定已完成，所以实际等待的时间可能会远远大于设定的期限，因为并不是所有任务在被取消后都会立刻结束。

```cpp
REQUIRE(co_await asyncio::timeout(asyncio::sleep(10ms), 20ms));
REQUIRE_ERROR(co_await asyncio::timeout(asyncio::sleep(20ms), 10ms), asyncio::TimeoutError::ELAPSED);
```

需要注意的是 `timeout` 返回的结果为两层 `std::expected`，外层的 `std::expected` 指示子任务是否在规定期限内完成了，里层的 `std::expected` 即为子任务的结果。

> `timeout` 是悲观的，这意味着即便超过了最后期限，但是在取消子任务时发生了错误，它依旧会返回子任务的结果，而不是 `TimeoutError::ELAPSED`。

`timeout` 只有在超过了期限，并且取消子任务成功时才会返回 `TimeoutError::ELAPSED`。
# Time

该模块提供时间相关的功能，包括休眠、超时控制等等。

## Function `sleep`

```c++
task::Task<void, std::error_code> sleep(std::chrono::milliseconds ms);
```

休眠一段时间后恢复执行。

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

期望某个任务在规定时间内完成，如果超过了期限则会取消任务，并等待其完成。

> `timeout` 保证在返回时子任务一定已完成，所以实际等待的时间可能会远远大于设定的期限，因为并不是所有任务在被取消后都会立刻结束。

对于基于错误码的 `Task<T, E>`，`timeout` 返回的结果为两层 `std::expected`，外层的 `std::expected` 指示子任务是否在规定期限内完成了，里层的 `std::expected` 即为子任务的结果。

```c++
REQUIRE(co_await asyncio::timeout(asyncio::sleep(10ms), 20ms));
REQUIRE_ERROR(co_await asyncio::timeout(asyncio::sleep(20ms), 10ms), asyncio::TimeoutError::Elapsed);
```

> `timeout` 是悲观的，这意味着即便超过了最后期限，但是在取消子任务时发生了错误，它依旧会返回子任务的结果，而不是 `TimeoutError::Elapsed`。

`timeout` 只有在超过了期限，并且取消子任务成功时才会返回 `TimeoutError::Elapsed`。

对于基于异常的 `Task<T>`，超时时会抛出携带调用栈的 `std::system_error`，子任务内部抛出的异常也会直接传播出来，不再需要处理两层 `std::expected`：

```c++
try {
    co_await asyncio::timeout(someTask(), 10ms);
} catch (const std::system_error &e) {
    if (e.code() == std::errc::timed_out)
        fmt::print(stderr, "Timed out\n");
}
```

> 无法确定超时错误是 `timeout` 抛出的，还是 `task` 内部抛出的，这是异常版本的缺陷。
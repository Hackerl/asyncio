# Thread

此模块提供线程相关的功能，用于兼容同步代码。

## Function `toThread`

```c++
template<std::invocable F>
task::Task<std::invoke_result_t<F>>
toThread(F f);

template<std::invocable F>
task::Task<std::invoke_result_t<F>>
toThread(F f, const std::function<std::expected<void, std::error_code>(std::thread::native_handle_type)> cancel);
```

将同步阻塞的代码放入新线程中运行，完成后返回对应的结果：

```c++
const auto result = co_await asyncio::toThread([] {
    std::this_thread::sleep_for(50ms);
    return 1024;
});
REQUIRE(result == 1024);
```

默认不支持取消，可提供自定义的取消函数：

```c++
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

```c++
Z_DEFINE_ERROR_CODE_EX(
    ToThreadPoolError,
    "asyncio::toThreadPool",
    Cancelled, "Request was cancelled", std::errc::operation_canceled
)

template<std::invocable F>
task::Task<std::invoke_result_t<F>, ToThreadPoolError>
toThreadPool(F f);

template<std::invocable F>
task::Task<std::invoke_result_t<F>, ToThreadPoolError>
toThreadPool(F f, const std::function<std::expected<void, std::error_code>()> cancel);
```

将耗时长的代码放入线程池中运行，完成后返回对应的结果：

```c++
const auto result = co_await asyncio::toThreadPool([] {
    std::this_thread::sleep_for(50ms);
    return 1024;
});
REQUIRE(result == 1024);
```

`toThreadPool` 底层使用的是 `uv_queue_work`，由 `libuv` 管理和调度线程。上层可以调用 `task.cancel()`，下层将使用 `uv_cancel` 尝试终止执行，如果任务还在队列之中并未开始，则取消成功并返回 `ToThreadPoolError::Cancelled` 错误。

`uv_cancel` 只能取消尚未开始的任务，一旦任务已在运行，`uv_cancel` 就会失败。第二个重载可以提供自定义的 `cancel` 函数作为降级方案，在 `uv_cancel` 失败时调用，用于通知正在运行的任务尽早退出：

```c++
bool exit{false};

co_await asyncio::toThreadPool(
    [&exit = std::as_const(exit)] {
        while (!exit) {
            std::this_thread::sleep_for(50ms);
        }
    },
    [&]() -> std::expected<void, std::error_code> {
        exit = true;
        return {};
    }
);
```

> 不应该将长时间阻塞的代码放入线程池中运行，因为线程池的数量是有限的，这会导致所有工作线程卡住。

## Function `toThreadPoolCatching`

```c++
template<std::invocable F>
task::Task<std::invoke_result_t<F>>
toThreadPoolCatching(F f);

template<std::invocable F>
task::Task<std::invoke_result_t<F>>
toThreadPoolCatching(F f, const std::function<std::expected<void, std::error_code>()> cancel);
```

与 `toThreadPool` 类似，但返回基于异常的 `Task<T>`。工作线程中抛出的异常会被捕获，并在协程恢复时重新抛出；取消错误也以异常形式抛出，而非返回错误码：

```c++
try {
    co_await asyncio::toThreadPoolCatching([] {
        throw std::runtime_error{"Something went wrong"};
    });
} catch (const std::exception &e) {
    fmt::print(stderr, "Exception: {}\n", e);
}
```

# Thread

此模块提供线程相关的功能，用于兼容同步代码。

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

将同步阻塞的代码放入新线程中运行，完成后返回对应的结果：

```cpp
const auto result = co_await asyncio::toThread([] {
    std::this_thread::sleep_for(50ms);
    return 1024;
});
REQUIRE(result == 1024);
```

默认不支持取消，可提供自定义的取消函数：

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
    CANCELLED, "request has been cancelled", std::errc::operation_canceled
)

template<typename F>
task::Task<std::invoke_result_t<F>, ToThreadPoolError>
toThreadPool(F f);
```

将耗时长的代码放入线程池中运行，完成后返回对应的结果：

```cpp
const auto result = co_await asyncio::toThreadPool([] {
    std::this_thread::sleep_for(50ms);
    return 1024;
});
REQUIRE(result == 1024);
```

`toThreadPool` 底层使用的是 `uv_queue_work`，由 `libuv` 管理和调度线程。上层可以调用 `task.cancel()`，下层将使用 `uv_cancel` 尝试终止执行，如果任务还在队列之中并未开始，则取消成功并返回 `ToThreadPoolError::CANCELLED` 错误。

> 不应该将长时间阻塞的代码放入线程池中运行，因为线程池的数量是有限的，这会导致所有工作线程卡住。
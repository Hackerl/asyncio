# Event Loop
该模块实现了 `Event Loop` 与相关功能。

## Class `EventLoop`
基于 `uv_loop_t` 封装的 `Event Loop`，几乎所有功能都依赖它，但你通常不会直接使用到它。

### Static Method `make`

```cpp
static std::expected<EventLoop, std::error_code> make();
```

创建一个全新的 `Event Loop`。

### Method `raw`

```cpp
uv_loop_t *raw();
[[nodiscard]] const uv_loop_t *raw() const;
```

返回底层的 `uv_loop_t` 指针。

### Method `post`

```cpp
std::expected<void, std::error_code> post(std::function<void()> function);
```

在下一次事件循环运行可调用对象。

### Method `run`

```cpp
void run();
```

开始运行，直到调用 `stop` 才停止。

### Method `stop`

```cpp
void stop();
```

停止运行。

## Function `run`

```cpp
template<typename F>
    requires zero::detail::is_specialization_v<std::invoke_result_t<F>, task::Task>
std::expected<
    std::expected<
        typename std::invoke_result_t<F>::value_type,
        typename std::invoke_result_t<F>::error_type
    >,
    std::error_code
>
run(F &&f);
```

新建一个 `Event Loop`，并在其上运行异步任务，任务完成后返回结果。

```cpp
const auto result = asyncio::run([]() -> asyncio::task::Task<int, std::error_code> {
    using namespace std::chrono_literals;
    co_await asyncio::sleep(10ms);
    co_return 1024;
});
REQUIRE(result == 1024);
```

> 它相当于 `Python` 的 `asyncio.run`。

通常我们会在 `main` 函数中使用 `asyncio::run` 运行我们的异步主函数，直到程序退出为止。当然也可以直接链接 `asyncio` 提供的 `main` 函数，它会自动调用名为 `asyncMain` 的异步主函数，请实现它并保证函数签名一致：

```cmake
target_link_libraries(demo PRIVATE asyncio::asyncio-main)
```

```cpp
asyncio::task::Task<void, std::error_code> asyncMain(const int, char *[]);
```

# Event Loop
该模块实现了 `Event Loop` 与相关功能。

## Class `EventLoop`

基于 `uv_loop_t` 封装的 `Event Loop`，几乎所有功能都依赖它，但你通常不会直接使用到它。

### Static Method `make`

```c++
static EventLoop make();
```

创建一个全新的 `Event Loop`。

### Method `raw`

```c++
uv_loop_t *raw();
[[nodiscard]] const uv_loop_t *raw() const;
```

返回底层的 `uv_loop_t` 指针。

### Method `post`

```c++
void post(std::function<void()> function);
```

在下一次事件循环执行可调用对象。

### Method `run`

```c++
void run();
```

开始运行，直到调用 `stop` 才停止。

### Method `stop`

```c++
void stop();
```

停止运行。

## Function `run`

```c++
template<typename F>
    requires zero::traits::is_specialization_v<std::invoke_result_t<F>, task::Task>
std::expected<
    typename std::invoke_result_t<F>::value_type,
    typename std::invoke_result_t<F>::error_type
>
run(const std::shared_ptr<EventLoop> &eventLoop, F &&f);

template<typename F>
    requires zero::traits::is_specialization_v<std::invoke_result_t<F>, task::Task>
auto run(F &&f) {
    return run(std::make_shared<EventLoop>(EventLoop::make()), std::forward<F>(f));
}
```

在指定的 `Event Loop` 上运行异步任务，等待任务完成并返回结果，不指定 `Event Loop` 则会创建一个新的 `Event Loop`。

对于 `Task<T, std::error_code>`，返回 `std::expected<T, std::error_code>`：

```c++
const std::expected<int, std::error_code> result = asyncio::run([]() -> asyncio::task::Task<int, std::error_code> {
    using namespace std::chrono_literals;
    Z_CO_EXPECT(co_await asyncio::sleep(10ms));
    co_return 1024;
});
REQUIRE(result);
REQUIRE(*result == 1024);
```

对于 `Task<T>`（基于异常），返回 `std::expected<T, std::exception_ptr>`：

```c++
const std::expected<int, std::exception_ptr> result = asyncio::run([]() -> asyncio::task::Task<int> {
    using namespace std::chrono_literals;
    zero::error::guard(co_await asyncio::sleep(10ms));
    co_return 1024;
});
REQUIRE(result);
REQUIRE(*result == 1024);
```

> 它相当于 `Python` 的 `asyncio.run`。

通常我们会在 `main` 函数中使用 `asyncio::run` 运行我们的异步主函数，直到程序退出为止。当然也可以直接链接 `asyncio` 提供的 `main` 函数，它会自动调用名为 `asyncMain` 的异步主函数，请实现它并保证函数签名一致：

```cmake
target_link_libraries(demo PRIVATE asyncio::asyncio-main)
```

```c++
asyncio::task::Task<void> asyncMain(int argc, char *argv[]);
```

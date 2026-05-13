# Event Loop
该模块实现了 `Event Loop` 与相关功能。

## Class `EventLoop`

基于 `uv_loop_t` 封装的 `Event Loop`，实现了 `zero::async::promise::IExecutor` 接口。几乎所有功能都依赖它，但你通常不会直接使用到它。

### Static Method `make`

```c++
static std::shared_ptr<EventLoop> make();
```

创建一个全新的 `Event Loop`，并以 `shared_ptr` 的形式返回。

### Method `raw`

```c++
uv_loop_t *raw();
[[nodiscard]] const uv_loop_t *raw() const;
```

返回底层的 `uv_loop_t` 指针。

### Method `post`

```c++
void post(std::function<void()> f) override;
```

实现 `zero::async::promise::IExecutor::post`。在下一次事件循环执行可调用对象。

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

### Method `submit`

```c++
template<typename F>
    requires (std::invocable<F> && !Invocable<F>)
SemiFuture<std::invoke_result_t<F>> submit(F &&f);

template<Invocable F>
SemiFuture<
    typename std::invoke_result_t<F>::value_type,
    typename std::invoke_result_t<F>::error_type
> submit(F &&f);
```

将可调用对象调度到事件循环上执行，并返回一个 `SemiFuture`。第一个重载接受任意普通可调用对象，第二个重载接受返回 `Task` 的协程函数。`SemiFuture` 尚未绑定执行器，可以阻塞等待或超时等待，也可以调用 `.via()` 绑定执行器后以回调方式处理结果。

> 在同步代码与异步事件循环跨线程交互时非常实用。

```c++
auto future = eventLoop->submit([] { return 1024; });

// 1. 无限期阻塞，直到事件循环完成任务
const auto value = *std::move(future).get();

// 2. 等待最多 5 秒，然后访问结果
zero::error::guard(future.wait(5s));  // 超时时抛出异常
assert(*future.result() == 1024);

// 3. 绑定到 inline executor 并附加回调（非阻塞）
std::move(future).via().then(
    [](const int value) {
        assert(value == 1024);
    },
    [](const std::exception &e) {
        fmt::print(stderr, "Exception: {}\n", e);
    }
);
```

## Function `reschedule`

```c++
task::Task<void, std::error_code> reschedule();
```

交出执行权，下一个事件循环再恢复执行，可以让其他待处理的回调或任务先运行：

```c++
co_await asyncio::reschedule();
```

## Function `run`

```c++
template<Invocable F>
std::expected<
    typename std::invoke_result_t<F>::value_type,
    typename std::invoke_result_t<F>::error_type
>
run(const std::shared_ptr<EventLoop> &eventLoop, F &&f);

template<Invocable F>
auto run(F &&f) {
    return run(EventLoop::make(), std::forward<F>(f));
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
    co_await asyncio::error::guard(asyncio::sleep(10ms));
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

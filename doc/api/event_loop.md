# Event Loop

This module implements the `Event Loop` and related functionality.

## Class `EventLoop`

An `Event Loop` encapsulated around `uv_loop_t`. Almost all functionality depends on it, but you typically won't use it directly.

### Static Method `make`

```c++
static EventLoop make();
```

Creates a new `Event Loop`.

### Method `raw`

```c++
uv_loop_t *raw();
[[nodiscard]] const uv_loop_t *raw() const;
```

Returns the underlying `uv_loop_t` pointer.

### Method `post`

```c++
void post(std::function<void()> function);
```

Executes a callable object on the next event loop iteration.

### Method `run`

```c++
void run();
```

Starts running until `stop` is called.

### Method `stop`

```c++
void stop();
```

Stops running.

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

Runs an async task on the specified `Event Loop`, waits for completion, and returns the result. If no `Event Loop` is specified, a new one will be created.

For `Task<T, std::error_code>`, returns `std::expected<T, std::error_code>`:

```c++
const std::expected<int, std::error_code> result = asyncio::run([]() -> asyncio::task::Task<int, std::error_code> {
    using namespace std::chrono_literals;
    Z_CO_EXPECT(co_await asyncio::sleep(10ms));
    co_return 1024;
});
REQUIRE(result);
REQUIRE(*result == 1024);
```

For `Task<T>` (exception-based), returns `std::expected<T, std::exception_ptr>`:

```c++
const std::expected<int, std::exception_ptr> result = asyncio::run([]() -> asyncio::task::Task<int> {
    using namespace std::chrono_literals;
    co_await asyncio::error::guard(asyncio::sleep(10ms));
    co_return 1024;
});
REQUIRE(result);
REQUIRE(*result == 1024);
```

> It's equivalent to Python's `asyncio.run`.

Typically, we use `asyncio::run` in the `main` function to run our async main function until program exit. Alternatively, you can directly link `asyncio`'s provided `main` function, which automatically calls an async main function named `asyncMain`. Implement it with a matching signature:

```cmake
target_link_libraries(demo PRIVATE asyncio::asyncio-main)
```

```c++
asyncio::task::Task<void> asyncMain(int argc, char *argv[]);
```

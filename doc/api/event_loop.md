# Event Loop

This module implements the `Event Loop` and related functionality.

## Class `EventLoop`

An `Event Loop` encapsulated around `uv_loop_t`. Almost all functionality depends on it, but you typically won't use it directly.

### Static Method `make`

```cpp
static EventLoop make();
```

Creates a new `Event Loop`.

### Method `raw`

```cpp
uv_loop_t *raw();
[[nodiscard]] const uv_loop_t *raw() const;
```

Returns the underlying `uv_loop_t` pointer.

### Method `post`

```cpp
std::expected<void, std::error_code> post(std::function<void()> function);
```

Executes a callable object on the next event loop iteration.

### Method `run`

```cpp
void run();
```

Starts running until `stop` is called.

### Method `stop`

```cpp
void stop();
```

Stops running.

## Function `run`

```cpp
template<typename F>
    requires zero::detail::is_specialization_v<std::invoke_result_t<F>, task::Task>
std::expected<
    typename std::invoke_result_t<F>::value_type,
    typename std::invoke_result_t<F>::error_type
>
run(F &&f);
```

Creates a new `Event Loop`, runs an async task on it, and returns the result after completion.

For `Task<T, std::error_code>`, returns `std::expected<T, std::error_code>`:

```cpp
const auto result = asyncio::run([]() -> asyncio::task::Task<int, std::error_code> {
    using namespace std::chrono_literals;
    Z_CO_EXPECT(co_await asyncio::sleep(10ms));
    co_return 1024;
});
REQUIRE(result);
REQUIRE(*result == 1024);
```

For `Task<T>` (exception-based), returns `std::expected<T, std::exception_ptr>`:

```cpp
const auto result = asyncio::run([]() -> asyncio::task::Task<int> {
    using namespace std::chrono_literals;
    zero::error::guard(co_await asyncio::sleep(10ms));
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

```cpp
asyncio::task::Task<void> asyncMain(int argc, char *argv[]);
```

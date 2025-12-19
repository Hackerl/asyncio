# Task

This module contains the core code for tasks.

## Error Code `Error`

```cpp
Z_DEFINE_ERROR_CODE_EX(
    Error,
    "asyncio::task",
    CANCELLED, "Task has been cancelled", std::errc::operation_canceled,
    CANCELLATION_NOT_SUPPORTED, "Task does not support cancellation", std::errc::operation_not_supported,
    LOCKED, "Task is locked", std::errc::resource_unavailable_try_again,
    CANCELLATION_TOO_LATE, "Operation will be done soon", Z_DEFAULT_ERROR_CONDITION
)
```

Common error types used in async tasks.

> For these common errors, it would be tedious to define them separately in each business code, though that would aid in error tracing.

## Class `Task`

Every async function must return a `Task` type.

```cpp
template<typename T, typename E = std::exception_ptr>
class Task;
```

> When the error type is `std::exception_ptr`, `Task`'s error handling becomes exception-based, typically used for APIs that should not fail.

### Method `cancel`

```cpp
std::expected<void, std::error_code> cancel();
```

Cancels the task. The cancellation operation applies to every branch of the task tree. If it fails, only the last failure reason is returned, but all tasks will still be marked as cancelled and will attempt to cancel again at the next suspension point when resumed.

```cpp
auto task = allSettled(task1, task2);
REQUIRE(task.cancel());

const auto result = co_await task;
REQUIRE_ERROR(result[0], std::errc::operation_canceled);
REQUIRE_ERROR(result[1], std::errc::operation_canceled);
```

### Method `callTree`

```cpp
tree<std::source_location> callTree() const;
```

Traces the entire task tree, obtaining the call stack of each subtask.

### Method `trace`

```cpp
[[nodiscard]] std::string trace() const
```

Traces the entire task tree, converting the `call tree` into a highly readable string.

### Method `addCallback`

```cpp
void addCallback(std::function<void()> callback);
```

Adds a `callback` associated with the task, which will be called when the task completes.

### Method `transform`

```cpp
template<typename F>
    requires (
        !std::is_same_v<E, std::exception_ptr> &&
        zero::detail::is_specialization_v<callback_result_t<F, T>, Task>
    )
Task<typename callback_result_t<F, T>::value_type, E> transform(F f) &&;

template<typename F>
    requires (
        !std::is_same_v<E, std::exception_ptr> &&
        !zero::detail::is_specialization_v<callback_result_t<F, T>, Task>
    )
Task<callback_result_t<F, T>, E> transform(F f) &&;
```

Transforms the value type when the task succeeds, equivalent to `std::expected::transform`. The handler function can be async.

> Only available when `E` is not `std::exception_ptr`.

### Method `andThen`

```cpp
template<typename F>
    requires (
        !std::is_same_v<E, std::exception_ptr> &&
        zero::detail::is_specialization_v<callback_result_t<F, T>, Task>
    )
Task<typename callback_result_t<F, T>::value_type, E> andThen(F f) &&;

template<typename F>
    requires (
        !std::is_same_v<E, std::exception_ptr> &&
        zero::detail::is_specialization_v<callback_result_t<F, T>, std::expected>
    )
Task<typename callback_result_t<F, T>::value_type, E> andThen(F f) &&;
```

Performs the next operation using the value when the task succeeds, equivalent to `std::expected::and_then`. The handler function can be async.

> Only available when `E` is not `std::exception_ptr`.

### Method `transformError`

```cpp
template<typename F>
    requires (
        !std::is_same_v<E, std::exception_ptr> &&
        zero::detail::is_specialization_v<callback_result_t<F, E>, Task>
    )
Task<T, typename callback_result_t<F, E>::value_type> transformError(F f) &&;

template<typename F>
    requires (
        !std::is_same_v<E, std::exception_ptr> &&
        !zero::detail::is_specialization_v<callback_result_t<F, E>, Task>
    )
Task<T, callback_result_t<F, E>> transformError(F f) &&;
```

Transforms the error type when the task fails, equivalent to `std::expected::transform_error`. The handler function can be async.

> Only available when `E` is not `std::exception_ptr`.

### Method `orElse`

```cpp
template<typename F>
    requires (
        !std::is_same_v<E, std::exception_ptr> &&
        zero::detail::is_specialization_v<callback_result_t<F, E>, Task>
    )
Task<T, typename callback_result_t<F, E>::error_type> orElse(F f) &&;

template<typename F>
    requires (
        !std::is_same_v<E, std::exception_ptr> &&
        zero::detail::is_specialization_v<callback_result_t<F, E>, std::expected>
    )
Task<T, typename callback_result_t<F, E>::error_type> orElse(F f) &&;
```

Performs the next operation using the error when the task fails, equivalent to `std::expected::or_else`. The handler function can be async.

> Only available when `E` is not `std::exception_ptr`.

### Method `done`

```cpp
[[nodiscard]] bool done() const;
```

Returns whether the task is complete.

### Method `cancelled`

```cpp
[[nodiscard]] bool cancelled() const;
```

Returns whether the task has been marked as cancelled.

### Method `lock`

```cpp
[[nodiscard]] bool lock() const;
```

Returns whether the task is locked.

> After a task is locked, all subtasks cannot be cancelled. Cancellation will return `asyncio::task::Error::LOCKED`.

### Method `future`

```cpp
zero::async::promise::Future<T, E> future();
```

Gets the `future` associated with the task, after which the task can no longer be `co_await`ed.

```cpp
auto task = asyncio::sleep(1s);

task.future()
    .then([] {
        fmt::print("Done\n");
    })
    .fail([](const auto &ec) {
        fmt:print("Unhandled error: {} ({})\n", ec.message(), ec);
    });;
```

> `Future` is similar to JavaScript's `Promise`, can bind callbacks, and supports aggregation operations like `all`.

## Struct `CancellableFuture`

```cpp
template<typename T, typename E>
struct CancellableFuture {
    zero::async::promise::Future<T, E> future;
    std::function<std::expected<void, std::error_code>()> cancel;
};
```

`Task` must interface with lower-level code and necessarily uses `Promise` and `Future` as bridges. However, `Promise` doesn't support cancellation, so we can provide a custom cancellation function.

```cpp
asyncio::task::Task<void, std::error_code> asyncio::sleep(const std::chrono::milliseconds ms) {
    auto ptr = std::make_unique<uv_timer_t>();

    Z_CO_EXPECT(uv::expected([&] {
        return uv_timer_init(getEventLoop()->raw(), ptr.get());
    }));

    uv::Handle timer{std::move(ptr)};

    Promise<void, std::error_code> promise;
    timer->data = &promise;

    Z_CO_EXPECT(uv::expected([&] {
        return uv_timer_start(
            timer.raw(),
            [](auto *handle) {
                uv_timer_stop(handle);
                static_cast<Promise<void, std::error_code> *>(handle->data)->resolve();
            },
            ms.count(),
            0
        );
    }));

    co_return co_await task::CancellableFuture{
        promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return std::unexpected{task::Error::CANCELLATION_TOO_LATE};

            uv_timer_stop(timer.raw());
            promise.reject(task::Error::CANCELLED);
            return {};
        }
    };
}
```

When the deadline hasn't been reached and the callback hasn't occurred, cancellation can be performed via `task.cancel()`.

> After a `Promise` is `resolve`d, the callback needs to wait until the next event loop, so we use `promise.isFulfilled()` to determine if the task is about to complete.

## Struct `CancellableTask`

```cpp
template<typename T, typename E>
struct CancellableTask {
    Task<T, E> task;
    std::function<std::expected<void, std::error_code>()> cancel;
};
```

A very small number of `Task`s cannot be cancelled, such as `ChildProcess::wait`:

```cpp
asyncio::task::Task<asyncio::process::ExitStatus, std::error_code> asyncio::process::ChildProcess::wait() {
    co_return co_await toThread(
        [this]() -> std::expected<ExitStatus, std::error_code> {
            int s{};

            const auto pid = this->impl().pid();
            const auto id = zero::os::unix::ensure([&] {
                return waitpid(pid, &s, 0);
            });
            Z_EXPECT(id);
            assert(*id == pid);

            return ExitStatus{s};
        }
    );
}
```

We create a new thread to blockingly execute `waitpid`, and we have no way to cancel it.

> Newer Linux kernels support `pidfd`, but I haven't tried it yet. The same goes for `macOS` - I know both can use OS-specific features to replace `waitpid`.

Regardless of whether a `Task` can be cancelled, we can override its cancellation function:

```cpp
co_await asyncio::task::CancellableTask{
    child->wait(),
    [&] {
        return child->kill();
    }
};
```

> Cancellation is top-down, traversing each branch of the task tree. When a cancellation function is overridden at a node, it will be called directly and returned early, then continue traversing the next branch.
> Of course, most `Task`s have `CancellableFuture` at their base, so most of the time you don't need to override.

## Constant `cancelled`

```cpp
struct Cancelled {
};

inline constexpr Cancelled cancelled;
```

In a `Task`, we can use `cancelled` to get the current task's cancellation status.

```cpp
asyncio::task::Task<void, std::error_code> asyncio::IWriter::writeAll(const std::span<const std::byte> data) {
    std::size_t offset{0};

    while (offset < data.size()) {
        if (co_await task::cancelled)
            co_return std::unexpected{task::Error::CANCELLED};

        const auto n = co_await write(data.subspan(offset));
        Z_CO_EXPECT(n);

        assert(*n != 0);
        offset += *n;
    }

    co_return {};
}
```

> All loops should manually check cancellation status, as we can't assume that any operation in a loop supports cancellation.
> If `write` doesn't support cancellation, we can only wait for `writeAll` to complete.

## Constant `lock`

```cpp
struct Lock {
};

inline constexpr Lock lock;
```

If we're performing atomic operations, such as writing critical content to a file on exit, we want to ensure these operations aren't interrupted. We can lock the current `Task`:

```cpp
co_await asyncio::task::lock;
// Write file.
co_await asyncio::task::unlock;
```

After locking, upper-level cancellation operations will fail and return `asyncio::task::Error::LOCKED`.

## Constant `unlock`

```cpp
struct Unlock {
};

inline constexpr Unlock unlock;
```

Used to unlock a `Task`.

> When a task is cancelled while locked, the cancellation operation will fail, but the task's state will still be marked as cancelled. It will automatically attempt to cancel at the first suspension point after unlocking.

## Class `TaskGroup`

Used to dynamically manage multiple tasks. It's like a combination of Golang's `WaitGroup` and `Context`, used to cancel and wait for multiple tasks.

> The difference from aggregation functions is that it can be used for dynamically created tasks of indeterminate number, and doesn't care about task results.

```cpp
asyncio::task::Task<void, std::error_code> serve(asyncio::net::TCPListener listener) {
    std::expected<void, std::error_code> result;
    asyncio::task::TaskGroup group;

    while (true) {
        auto stream = co_await listener.accept();

        if (!stream) {
            result = std::unexpected{stream.error()};
            break;
        }

        auto task = handle(*std::move(stream));

        group.add(task);
        task.future().fail([](const auto &ec) {
            fmt::print(stderr, "Unhandled error: {} ({})\n", ec.message(), ec);
        });
    }

    co_await group;
    co_return result;
}
```

There's no better use case than a `TCP Server`. The server continuously accepts new connections, each requiring a subtask for handling. We can't wait for subtasks, but we can't completely discard them either. We want all subtasks' lifecycles to end when the function returns, so we use `TaskGroup` to manage them.

> When an upper-level cancels the task, `co_await group` will automatically cancel all subtasks in the group and wait for them to complete.

### Method `cancelled`

```cpp
[[nodiscard]] bool cancelled() const;
```

Returns whether the task group has been cancelled.

### Method `cancel`

```cpp
std::expected<void, std::error_code> cancel();
```

Cancels the task group. The cancellation operation applies to all tasks in the group. If it fails, only the last failure reason is returned, but all tasks will still be marked as cancelled and will attempt to cancel again at the next suspension point when resumed.

> After a task group is marked as cancelled, newly added subtasks will be cancelled immediately.

### Method `add`

```cpp
template<typename T>
    requires zero::detail::is_specialization_v<std::remove_cvref_t<T>, Task>
void add(T &&task);
```

Adds a task to the group.

> You can keep adding tasks to the group. Tasks are automatically removed from the group upon completion, so there's no need to worry about overflow or excessive memory usage.

## Function `all`

Waits for all tasks. Returns success if all tasks succeed. Returns failure and cancels remaining tasks if any task fails.

> Each aggregation function has three overloads: parameters can be an `iterator` or `range`, or a variadic parameter pack (supporting different task types). See the unit tests in this project for specific usage.
> All aggregation functions guarantee that when the function returns, all subtasks have completed.

```cpp
template<std::input_iterator I, std::sentinel_for<I> S>
    requires zero::detail::is_specialization_v<std::iter_value_t<I>, Task>
Task<
    all_ranges_value_t<I, S>,
    all_ranges_error_t<I, S>
>
all(I first, S last);

template<std::ranges::range R>
    requires zero::detail::is_specialization_v<std::ranges::range_value_t<R>, Task>
auto all(R &&tasks) {
    return all(tasks.begin(), tasks.end());
}
```

- When subtask type is `Task<void, E>`, return value type is `Task<void, E>`.
- When subtask type is `Task<T, E>`, return value type is `Task<std::vector<T>, E>`.

```cpp
template<typename... Ts>
    requires (zero::detail::is_specialization_v<std::remove_cvref_t<Ts>, Task> && ...)
Task<
    all_variadic_value_t<Ts...>,
    all_variadic_error_t<Ts...>
>
all(Ts &&... tasks);
```

- When all subtask types are `Task<void, E>`, return value type is `Task<void, E>`.
- When all subtask types are `Task<T, E>`, return value type is `Task<std::array<T, N>, E>`.
- When subtask types differ, return value type is `Task<std::tuple<T1, T2, ...>, E>`. `std::tuple` cannot contain `void`, so if `T` is `void`, it will be converted to `std::nullptr_t`.

## Function `allSettled`

Waits for all tasks to complete, returns all task results, never fails.

```cpp
template<std::input_iterator I, std::sentinel_for<I> S>
    requires zero::detail::is_specialization_v<std::iter_value_t<I>, Task>
Task<all_settled_ranges_value_t<I, S>>
allSettled(I first, S last);

template<std::ranges::range R>
    requires zero::detail::is_specialization_v<std::ranges::range_value_t<R>, Task>
auto allSettled(R &&tasks) {
    return allSettled(tasks.begin(), tasks.end());
}
```

Return value type is always `std::vector<Task<T, E>>`.

```cpp
template<typename... Ts>
    requires (zero::detail::is_specialization_v<std::remove_cvref_t<Ts>, Task> && ...)
Task<all_settled_variadic_value_t<Ts...>>
allSettled(Ts &&... tasks);
```

Return value type is always `std::tuple<Task<T1, E>, Task<T2, E>, ...>`.

## Function `any`

Succeeds if any task succeeds, cancelling remaining tasks.

```cpp
template<std::input_iterator I, std::sentinel_for<I> S>
    requires zero::detail::is_specialization_v<std::iter_value_t<I>, Task>
Task<
    any_ranges_value_t<I, S>,
    any_ranges_error_t<I, S>
>
any(I first, S last);

template<std::ranges::range R>
    requires zero::detail::is_specialization_v<std::ranges::range_value_t<R>, Task>
auto any(R &&tasks) {
    return any(tasks.begin(), tasks.end());
}
```

Return value type is always `Task<T, std::vector<E>>`.

```cpp
template<typename... Ts>
    requires (zero::detail::is_specialization_v<std::remove_cvref_t<Ts>, Task> && ...)
Task<
    any_variadic_value_t<Ts...>,
    any_variadic_error_t<Ts...>
>
any(Ts &&... tasks);
```

- When all subtask types are `Task<T, E>`, return value type is `Task<T, std::vector<E>>`.
- When subtask types differ, return value type is `Task<std::any, std::vector<E>>`.

## Function `race`

Uses the fastest-completing task as the result, cancelling other tasks.

```cpp
template<std::input_iterator I, std::sentinel_for<I> S>
    requires zero::detail::is_specialization_v<std::iter_value_t<I>, Task>
Task<
    race_ranges_value_t<I, S>,
    race_ranges_error_t<I, S>
>
race(I first, S last);

template<std::ranges::range R>
    requires zero::detail::is_specialization_v<std::ranges::range_value_t<R>, Task>
auto race(R &&tasks) {
    return race(tasks.begin(), tasks.end());
}
```

Return value type is always `Task<T, E>`.

```cpp
template<typename... Ts>
    requires (zero::detail::is_specialization_v<std::remove_cvref_t<Ts>, Task> && ...)
Task<
    race_variadic_value_t<Ts...>,
    race_variadic_error_t<Ts...>
>
race(Ts &&... tasks);
```

- When all subtask types are `Task<T, E>`, return value type is `Task<T, E>`.
- When subtask types differ, return value type is `Task<std::any, E>`.

## Function `from`

```cpp
template<typename T, typename E>
Task<T, E> from(zero::async::promise::Future<T, E> future);

template<typename T, typename E>
Task<T, E> from(zero::async::promise::CancellableFuture<T, E> future);

template<typename T, typename E>
Task<T, E> from(zero::async::promise::CancellableTask<T, E> task);
```

Converts `Future`, `CancellableFuture`, `CancellableTask` to `Task`. The `asyncio` APIs are designed for `Task`, so conversion is needed before calling.

```cpp
const auto status = zero::flattenWith<std::error_code>(
    co_await asyncio::timeout(
        from(asyncio::task::CancellableTask{
            child->wait(),
            [&] {
                return child->kill();
            }
        }),
        WAIT_TIMEOUT
    )
);
```

## Function `spawn`

```cpp
template<typename F>
    requires zero::detail::is_specialization_v<std::invoke_result_t<F>, Task>
std::invoke_result_t<F> spawn(F f) {
    co_return co_await f();
}
```

Creates a task from a callable object. It seems to do nothing but simply call - why not call it directly in the outer layer?

```cpp
asyncio::task::Task<void> func(std::string host) {
    auto task = [=]() -> asyncio::task::Task<void> {
        auto socket = co_await connect(host, 443);
        co_await socket->write(xxx);
        std::cout << host << std::endl;
    }();

    co_await task;
}
```

Using a capturing lambda to create a coroutine, when executing `co_await task`, the temporary lambda object has been destroyed, and the captured `host`'s lifetime has ended. The unfinished `task` will access an unknown value after resuming, and although most of the time the data on the stack hasn't been overwritten and the memory `host` points to is still valid, this hidden danger can cause unknown risks.

Although extremely inelegant, if you want to use a capturing lambda to create a coroutine, you can only explicitly pass parameters:

```cpp
Task<void> func(std::string host) {
    auto task = [](auto host) -> Task<void> {
        auto socket = co_await connect(host, 443);
        co_await socket->write(xxx);
        std::cout << host << std::endl;
    }(host);

    co_await task;
}
```

> [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rcoro-capture) recommends using regular functions to create coroutines whenever possible.

We can also use `deducing this` to solve this problem, but compiler support isn't complete:

```cpp
auto task = [host]<typename Self>(this Self self) -> asyncio::task::Task<void> {
    auto socket = co_await connect(self.host, 443);
    co_await socket->write(xxx);
    std::cout << self.host << std::endl;
}();
```

So we use the `spawn` function to create tasks, saving the temporary lambda on `spawn`'s coroutine stack to extend its lifetime:

```cpp
asyncio::task::spawn([=]() -> asyncio::task::Task<void> {
    auto socket = co_await connect(host, 443);
    co_await socket->write(xxx);
    std::cout << host << std::endl;
});
```

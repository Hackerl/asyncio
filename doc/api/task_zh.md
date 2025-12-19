# Task

此模块为任务的核心代码。

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

异步任务中通用的错误类型。

> 对于这些常用的错误，在每个业务代码处都单独定义一个是麻烦且枯燥的，虽然它利于错误追溯。

## Class `Task`
每一个异步函数的返回值都必须是 `Task` 类型。

```cpp
template<typename T, typename E = std::exception_ptr>
class Task;
```

> 当错误类型是 `std::exception_ptr` 时，`Task` 的错误处理将变为异常，通常用于那些不应该失败的 `API`。

### Method `cancel`

```cpp
std::expected<void, std::error_code> cancel();
```

取消任务，取消操作将会作用到任务树的每一个分支上；如果失败，仅返回最后一次失败的原因，所有任务依旧会被标记为取消状态，恢复运行后将在下一次挂起点再次尝试取消。

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

追溯整个任务树，可以获取每一个子任务的调用栈。

### Method `trace`

```cpp
[[nodiscard]] std::string trace() const
```

追溯整个任务树，将 `call tree` 转为了可读性高的字符串。

### Method `addCallback`

```cpp
void addCallback(std::function<void()> callback);
```

添加任务关联的 `callback`，将在任务完成时调用。

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

当任务成功时转换值的类型，相当于 `std::expected::transofrm`，处理函数可以是异步的。

> 仅当 `E` 不为 `std::exception_ptr` 时可用。

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

当任务成功时使用值进行下一步操作，相当于 `std::expected::and_then`，处理函数可以是异步的。

> 仅当 `E` 不为 `std::exception_ptr` 时可用。

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

当任务失败时转换错误类型，相当于 `std::expected::transform_error`，处理函数可以是异步的。

> 仅当 `E` 不为 `std::exception_ptr` 时可用。

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

当任务失败时使用错误进行下一步操作，相当于 `std::expected::or_else`，处理函数可以是异步的。

> 仅当 `E` 不为 `std::exception_ptr` 时可用。

### Method `done`

```cpp
[[nodiscard]] bool done() const;
```

返回任务是否完成。

### Method `cancelled`

```cpp
[[nodiscard]] bool cancelled() const;
```

返回任务是否已标记为取消。

### Method `cancelled`

```cpp
[[nodiscard]] bool lock() const;
```

返回任务是否已被锁定。

> 任务被锁定之后，所有子任务均无法被取消，取消时返回 `asyncio::task::Error::LOCKED`。

### Method `future`

```cpp
zero::async::promise::Future<T, E> future();
```

获取任务关联的 `future`，之后任务不能再被 `co_await`。

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

> `Future` 类似于 `JavaScript` 的 `Promise`，可以绑定回调，也支持 `all` 等聚合操作。

## Struct `CancellableFuture`

```cpp
template<typename T, typename E>
struct CancellableFuture {
    zero::async::promise::Future<T, E> future;
    std::function<std::expected<void, std::error_code>()> cancel;
};
```

`Task` 与底层接口打交道，必然要使用 `Promise` 和 `Future` 作为桥梁，然而 `Promise` 是不支持取消的，所以我们可以提供自定义的取消函数。

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

当截止时间还未达到，回调还没发生时，可以通过 `task.cancel()` 进行取消。

> `Promise` 被 `resolve` 后，回调需要等待到下一次事件循环，所以我们通过 `promise.isFulfilled()` 进行判断任务是否即将完成。

## Struct `CancellableTask`

```cpp
template<typename T, typename E>
struct CancellableTask {
    Task<T, E> task;
    std::function<std::expected<void, std::error_code>()> cancel;
};
```

极少数的 `Task` 是无法被取消的，例如 `ChildProcess::wait`：

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

我们创建一个新线程用于阻塞地执行 `waitpid`，同时我们没有任何办法取消它。

> 新的 `Linux` 内核支持 `pidfd`，但我还没有尝试它；包括 `macOS` 也一样，我知道它们都可以使用操作系统特有的功能来替换 `waitpid`。

无论 `Task` 能否取消，我们都可以重载它的取消函数：

```cpp
co_await asyncio::task::CancellableTask{
    child->wait(),
    [&] {
        return child->kill();
    }
};
```

> 取消操作是自顶而下的，依次遍历任务树的每一个分支，当某个节点上重载了取消函数时，将会直接调用它并提早返回，然后继续遍历下一个分支。
> 当然，大部分 `Task` 的底部都是 `CancellableFuture`，所以绝大多数时候你不必进行重载。

## Constant `cancelled`

```cpp
struct Cancelled {
};

inline constexpr Cancelled cancelled;
```

在 `Task` 中，我们可以使用 `cancelled` 来获取当前任务的取消状态。

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

> 所有涉及循环的地方都应该手动检查取消状态，因为我们不能假定循环中的任意操作都是支持取消的。
> 如果 `write` 不支持取消，那么我们只能傻傻地等待 `writeAll` 完成。

## Constant `lock`

```cpp
struct Lock {
};

inline constexpr Lock lock;
```

如果我们正在执行一些原子操作，例如退出时，我们需要将至关重要的内容写入文件，我们想确保这些操作不会被中断，那么我们可以锁定当前 `Task`：

```cpp
co_await asyncio::task::lock;
// Write file.
co_await asyncio::task::unlock;
```

锁定之后，上层的取消操作将失败，并返回 `asyncio::task::Error::LOCKED`。

## Constant `unlock`

```cpp
struct Unlock {
};

inline constexpr Unlock unlock;
```

用于解开 `Task` 的锁定。

> 任务被锁定时取消，取消操作将失败，但任务的状态依旧会被标记为已取消，将在解锁后的第一个挂起点处自动尝试取消。

## Class `TaskGroup`

用于动态管理多个任务，它就像是 `Golang` 中 `WaitGroup` 和 `Context` 的结合体，用于取消、等待多个任务。

> 它和聚合函数的区别是，它可以用于动态创建、数量不定的任意任务，并且不关心任务的结果。

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

没有比 `TCP Server` 更适合它的应用场景了，服务端不断地接收新的连接，每个连接都需要创建一个子任务进行处理。我们不能等待子任务，但又不能完全丢弃它，我们希望函数返回时所有子任务的生命周期也随之结束，所以我们使用 `TaskGroup` 来管理它们。

> 当上层取消任务后，`co_await group` 将会自动取消组内所有子任务，并等待它们完成。

### Method `cancelled`

```cpp
[[nodiscard]] bool cancelled() const;
```

返回任务组是否已取消。

### Method `cancel`

```cpp
std::expected<void, std::error_code> cancel();
```

取消任务组，取消操作将会作用到组内所有任务上；如果失败，仅返回最后一次失败的原因，所有任务依旧会被标记为取消状态，恢复运行后将在下一次挂起点再次尝试取消。

> 任务组被标记为取消后，新加入的子任务将会立刻被取消。

### Method `add`

```cpp
template<typename T>
    requires zero::detail::is_specialization_v<std::remove_cvref_t<T>, Task>
void add(T &&task);
```

添加一个任务到组内。

> 可以一直往组内添加任务，任务完成后会自动从组内移除，所以不必担心它会溢出或占用太多内存。

## Function `all`

等待所有任务，所有任务都成功时返回成功，任何一个失败则返回失败并取消剩余任务。

> 每一个聚合函数都有三种重载，参数可以是 `iterator` 或 `range`，也可以是可变参数包（支持不同的任务类型），具体使用方式请参考此项目的单元测试。
> 所有聚合函数都保证，函数返回时所有子任务都已完成。

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

- 子任务类型是 `Task<void, E>` 时，返回值类型是 `Task<void, E>`。
- 子任务类型是 `Task<T, E>` 时，返回值类型是 `Task<std::vector<T>, E>`。

```cpp
template<typename... Ts>
    requires (zero::detail::is_specialization_v<std::remove_cvref_t<Ts>, Task> && ...)
Task<
    all_variadic_value_t<Ts...>,
    all_variadic_error_t<Ts...>
>
all(Ts &&... tasks);
```

- 子任务类型都是 `Task<void, E>` 时，返回值类型是 `Task<void, E>`。
- 子任务类型都是 `Task<T, E>` 时，返回值类型是 `Task<std::array<T, N>, E>`。
- 子任务类型不同时，返回值类型是 `Task<std::tuple<T1, T2, ...>, E>`，`std::tuple` 中不能包含 `void`，所以如果 `T` 是 `void`，那么它将被转为 `std::nullptr_t`。

## Function `allSettled`

等待所有任务完成，返回所有任务结果，永远不会失败。

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

返回值类型永远是 `std::vector<Task<T, E>>`。

```cpp
template<typename... Ts>
    requires (zero::detail::is_specialization_v<std::remove_cvref_t<Ts>, Task> && ...)
Task<all_settled_variadic_value_t<Ts...>>
allSettled(Ts &&... tasks);
```

返回值类型永远是 `std::tuple<Task<T1, E>, Task<T2, E>, ...>`。

## Function `any`

任何一个任务成功则成功，并取消剩余任务。

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

返回值类型永远是 `Task<T, std::vector<E>>`。

```cpp
template<typename... Ts>
    requires (zero::detail::is_specialization_v<std::remove_cvref_t<Ts>, Task> && ...)
Task<
    any_variadic_value_t<Ts...>,
    any_variadic_error_t<Ts...>
>
any(Ts &&... tasks);
```

- 子任务类型都是 `Task<T, E>` 时，返回值类型是 `Task<T, std::vector<E>>`。
- 子任务类型不同时，返回值类型是 `Task<std::any, std::vector<E>>`。

## Function `race`

使用最快完成的作为结果，并取消其它任务。

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

返回值类型永远是 `Task<T, E>`。

```cpp
template<typename... Ts>
    requires (zero::detail::is_specialization_v<std::remove_cvref_t<Ts>, Task> && ...)
Task<
    race_variadic_value_t<Ts...>,
    race_variadic_error_t<Ts...>
>
race(Ts &&... tasks);
```

- 子任务类型都是 `Task<T, E>` 时，返回值类型是 `Task<T, E>`。
- 子任务类型不同时，返回值类型是 `Task<std::any, E>`。

## Function `from`

```cpp
template<typename T, typename E>
Task<T, E> from(zero::async::promise::Future<T, E> future);

template<typename T, typename E>
Task<T, E> from(zero::async::promise::CancellableFuture<T, E> future);

template<typename T, typename E>
Task<T, E> from(zero::async::promise::CancellableTask<T, E> task);
```

将 `Future`、`CancellableFuture`、`CancellableTask` 转换为 `Task`；`asyncio` 的 `API` 都是针对 `Task` 的，转换后我们才能调用。

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

从可调用对象创建任务。它好像什么也没做，只是简单地调用，那为什么我们不在外层直接调用呢？

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

使用具有捕获的 `lambda` 创建协程，在执行 `co_await task` 时，临时的 `lambda` 对象已经消亡，而它捕获的 `host` 生命周期也随之结束；那么未结束的 `task` 在恢复执行后将会访问到一个未知值，虽然大多数情况下栈上的数据还没有被改写，`host` 所指向的内存还是有效的，但恰恰是这暗藏祸根的代码会带来未知的风险。

虽然极其不优雅，但是如果想使用具有捕获的 `lambda` 创建协程只能显式地传参：

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

> [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rcoro-capture) 建议尽量使用普通函数创建协程。

我们还可以使用 `deducing this` 来解决这个问题，但是编译器的支持并不完善：

```cpp
auto task = [host]<typename Self>(this Self self) -> asyncio::task::Task<void> {
    auto socket = co_await connect(self.host, 443);
    co_await socket->write(xxx);
    std::cout << self.host << std::endl;
}();
```

所以我们使用 `spawn` 函数来创建任务，将临时的 `lambda` 保存在 `spawn` 的协程栈上以延长其生命周期：

```cpp
asyncio::task::spawn([=]() -> asyncio::task::Task<void> {
    auto socket = co_await connect(host, 443);
    co_await socket->write(xxx);
    std::cout << host << std::endl;
});
```
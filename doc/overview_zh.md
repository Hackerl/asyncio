# 概要

## 任务

`Task` 是 `asyncio` 中最核心的一个组件：

```c++
asyncio::task::Task<void> test1() {
    while (true) {
        if (co_await task::cancelled)
            throw zero::error::SystemError{task::Error::CANCELLED};

        zero::error::guard(co_await asyncio::sleep(1s));
        fmt::print("hello world\n");
    }
}

asyncio::task::Task<void, std::error_code> test2() {
    while (true) {
        if (co_await task::cancelled)
            co_return std::unexpected{task::Error::CANCELLED};

        Z_CO_EXPECT(co_await asyncio::sleep(1s));
        fmt::print("hello world\n");
    }
}
```

> 任务支持异常和错误码两种处理方式，可以选择你更喜欢的那一种。

任务的底层是基于状态机的无栈协程，将它运行起来后它并不会立刻完成，它可能需要持续地挂起与恢复。等到它真正完成的那一刻，它所关联的 `promise` 便会立刻 `resolve`。
我们可以在一个任务中等待另一个任务，`asyncio::sleep(1s)` 就是一个任务，这个任务会在 1 秒后完成。当 `co_await asyncio::sleep(1s)` 执行时，当前任务观察到 `sleep` 的任务创建后并没有立刻完成，它便要将自身挂起交出控制权，等到 `sleep` 完成后再获取它的结果并恢复执行：

```c++
auto task = asyncio::sleep(1s);

if (!task.done) {
    task.future().then([handle = currentTaskHandle] {
        handle.resume();
    });
    suspend();
}

// 任务已完成，继续执行。
tag:
    fmt::print("hello world\n");
```

它的执行流程大概就是这样，它将自身挂起后将回调函数绑定到 `sleep` 的 `future` 上，`sleep` 任务完成后 `promise` 被解决时，`future` 上绑定的回调函数就会被执行。

> `handle.resume()` 执行时便会立刻跳转到 `tag` 标签处开始继续运行。

## 事件循环

你可能已经开始好奇 `sleep` 这个任务的内部是怎么样的？它是怎么让自己在 1 秒之后完成的，在 1 秒的时间内它自身又处于一种什么样的状态呢？
在揭开这个谜底之前，我们需要先了解一些背景。`asyncio` 底层使用的是 `libuv` 的事件循环，主线程创建事件循环之后，会接着启动顶层任务，然后阻塞于 `uv_run` 直到任务完成：

```c++
namespace asyncio {
    template<typename F>
        requires zero::detail::is_specialization_v<std::invoke_result_t<F>, task::Task>
    std::expected<
        std::expected<
            typename std::invoke_result_t<F>::value_type,
            typename std::invoke_result_t<F>::error_type
        >,
        std::error_code
    >
    run(F &&f) {
        const auto eventLoop = std::make_shared<EventLoop>(EventLoop::make());
        setEventLoop(eventLoop);

        auto future = f().future().finally([&] {
            eventLoop->stop();
        });

        eventLoop->run();
        assert(future.isReady());
        return {std::move(future).result()};
    }
}

void asyncio::EventLoop::run() {
    uv_run(mLoop.get(), UV_RUN_DEFAULT);
}
```

> 顶层任务完成后，`future` 绑定的回调函数被调用，事件循环被停止。

如果你曾使用过 `libuv`，你可能已经猜到了 `sleep` 内部将会使用到 `uv_timer`：

```c++
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
                zero::error::guard(uv::expected([&] {
                    return uv_timer_stop(handle);
                }));
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
                return std::unexpected{task::Error::WILL_BE_DONE};

            zero::error::guard(uv::expected([&] {
                return uv_timer_stop(timer.raw());
            }));

            promise.reject(task::Error::CANCELLED);
            return {};
        }
    };
}
```

`uv_timer_t` 是 `libuv` 中的定时器组件，我们创建它并设置延迟时间，然后绑定回调函数。时间一到，回调函数会立刻被执行。
`sleep` 当然也是一个任务，在期限未到之前它同样需要被挂起，时限到了之后它又需要被唤醒。所以我们使用一个手动构造的 `promise` 来充当起这个桥梁，`sleep` 等待这个 `promise` 完成，回调函数中则 `resolve` 这个 `promise`。
所以在 1 秒的时间内，`sleep` 这个任务因等待 `promise` 而被挂起，时间一到，`promise` 被 `resolve` 便又恢复执行。
它的恢复执行也意味着任务的结束，因等待它而被挂起的任务也会逐渐苏醒过来。

## 取消

如果程序启动了一个 `sleep(1h)` 的任务，但此时又遇到了错误需要退出该怎么办呢？优雅地退出首先要等待所有的子任务完成，当然，我们不可能傻傻地干等一个小时，那任务可以被取消吗？
当然可以，在大多数协程库的设计中，都使用 `context` 来传递取消信号：

```go
select {
case <-time.After(2 * time.Second):
    // If we receive a message after 2 seconds
    // that means the request has been processed
    // We then write this as the response
    w.Write([]byte("request processed"))
case <-ctx.Done():
    // If the request gets cancelled, log it
    // to STDERR
    fmt.Fprint(os.Stderr, "request cancelled\n")
}
```

但是，从项目的名称就能看出来，我选择的方式并不是这种。

```c++
auto task = asyncio::sleep(1h);
task.cancel();
```

> `task.cancel()` 是 `Python3` 异步任务的取消方式，我喜欢这种简单直接的做法。

一个任务等待另一个任务的完成，它们一个个串连起来就像一根链条，当你在顶端调用 `cancel` 时，会一路摸索到链条的尾部，并执行具体的取消操作：

```c++
asyncio::task::Task<void, std::error_code> asyncio::sleep(const std::chrono::milliseconds ms) {
    // ...
    co_return co_await task::CancellableFuture{
        promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return std::unexpected{task::Error::WILL_BE_DONE};

            zero::error::guard(uv::expected([&] {
                return uv_timer_stop(timer.raw());
            }));

            promise.reject(task::Error::CANCELLED);
            return {};
        }
    };
}
```

如果你阅读得足够仔细，那你一定已经看到了 `sleep` 中注册的取消函数了。`uv_timer_stop` 停止定时器，`promise.reject(task::Error::CANCELLED)` 让 `promise` 返回一个取消错误。

> 为什么要判断 `promise.isFulfilled()` 呢？因为 `promise` 完成后并不会立刻回调，它需要等待下一个事件循环，而在这个时间段内已无法再取消。
> 从这里也可以看出来，取消操作并不一定能成功，出错时 `task.cancel()` 将会返回 `std::error_code`。

取消成功后，`CANCELLED` 错误从 `sleep` 中被返回时，上层的 `Z_CO_EXPECT(sleep(1s))` 会接着将错误上抛，一层一层传递，直到最后被处理。

大多数协程库选择 `context` 的理由是为了更细粒度地控制任务，因为在实际应用中，任务的结构并不会是一条简简单单的链条，它通常都是一棵繁茂多枝的树。
当 `context` 被传递到无数个错综复杂的子任务中后，在顶层执行取消操作，取消的通知便能顺流而下传递到每一个分支中。
那难道 `task.cancel()` 就无法做到更加细粒度的控制吗？难道它只能取消单一的任务链条吗？

## 任务树

在编写一个复杂的程序时，任务结构绝不会是简单的、线性的，我们可能需要同时等待多个子任务，等待它们全部成功或者任意一个成功。
借鉴于 `JavaScript` 的 `Promise`，在 `asyncio` 中我们也可以使用相同的方式来等待多个子任务。

```c++
// test1 和 test2 可以具有不同的值类型，但必须具有相同的错误类型。
co_await asyncio::task::all(test1(), test2(), ...);
co_await asyncio::task::allSettled(test1(), test2(), ...);
co_await asyncio::task::any(test1(), test2(), ...);
co_await asyncio::task::race(test1(), test2(), ...);

// test1 和 test2 必须具有相同的结果类型。
co_await asyncio::task::all(std::vector{test1(), test2()});
co_await asyncio::task::allSettled(std::vector{test1(), test2()});
co_await asyncio::task::any(std::vector{test1(), test2()});
co_await asyncio::task::race(std::vector{test1(), test2()});
```

- all：所有任务成功则成功，任何一个失败则返回失败并取消剩余任务。
- allSettled：等待所有任务完成，永远不会失败。
- any：任何一个任务成功则成功并取消剩余任务。
- race：使用最快完成的作为结果，并取消其它任务。

> 当上诉函数返回时，保证所有子任务都已完成。
> 更多详细的说明会在后续章节补充，不在此赘述。

这些聚合操作将多个子任务合并成了一个子任务，整个任务结构就像是一棵树，从顶端不停地开枝散叶。我们在上层等待的实际上是一棵子树，取消任务时当然需要将它的所有分支都取消掉。

```c++
auto task = asyncio::task::all(test1(), test2(), ...);
task.cancel();
```

任务聚合之后，`asyncio` 会在底层默默记下这一状况，但对于使用者来说并不需要去在意这些细节。当 `task.cancel()` 被调用后，`test1` 与 `test2` 以及它们所有的子任务均会被取消。

```text
task
├── test1
│   ├── test3
│   └── test4
└── test2
    ├── test5
    └── test6
        └── test7
```

取消操作将在子树的每一个叶子节点（3、4、5、7）上实施。

> 每个叶子节点上的取消都可能会失败，失败时 `task.cancel()` 返回最后一次出现的错误。

## 任务组

子任务是如何聚合成一个任务的呢？子任务又是如何被管理的呢？
在回答这个问题之前，我们先看一个例子：

```c++
asyncio::task::Task<void, std::error_code> handle(asyncio::net::TCPStream stream) {
    // ...
    co_return {};
}

asyncio::task::Task<void, std::error_code> serve(asyncio::net::TCPListener listener) {
    while (true) {
        auto stream = co_await listener.accept();
        Z_CO_EXPECT(stream);

        handle(*std::move(stream)).future().fail([](const auto &ec) {
            fmt::print(stderr, "Unhandled error: {} ({})\n", ec.message(), ec);
        });
    }
}

asyncio::task::Task<void, std::error_code> asyncMain(const int argc, char *argv[]) {
    auto listener = asyncio::net::TCPListener::listen("127.0.0.1", 8000);
    Z_CO_EXPECT(listener);

    auto signal = asyncio::Signal::make();

    co_return co_await race(
        serve(*std::move(listener)),
        signal.on(SIGINT).transform([](const int) {
        })
    );
}
```

上面是一个简单的 `TCP` 服务，`serve` 子任务中会不停地进行 `accept` 接收新的连接，然后启动一个 `handle` 任务进行处理。
启动 `handle` 之后我们并不会等待它，因为我们需要不停地 `accept`， 不能阻塞于此。如此一来， `handle` 任务的会不会返回错误我们也就不得而知了，所以我们在任务的 `future` 上绑定了回调函数用来打印错误。

当终端按下 `ctrl + c` 时，`race` 中监听信号的子任务将会完成，那么 `race` 就会返回成功并取消掉 `serve` 子任务，程序从主函数返回并退出。
看起来似乎已经完美了，但美中不足的是主函数退出时，会有 N 个 `handle` 任务可能还在运行之中。这种场景下不会有什么问题，但如果 `handle` 引用了某个资源，而程序在退出过程中销毁了该资源，则会造成意想不到的错误。
我们当然不希望这种情况发生，最完美的退出也许是 `serve` 在完成之前，取消掉所有 `handle` 子任务并等待它们完成。但 `handle` 任务的数量是未知的，可能寥寥无几，也可能成千上万，更重要的是它是动态变化、实时创建的，我们无法使用 `all` 等聚合函数进行管理，那么该怎么办呢？
为了实现这一目标，我引入了 `Task Group` 的概念，同时它也是 `all` 等聚合操作底层使用的组件。
我们可以往任务组中动态地添加无数个任务，任务完成时自动从组内移除。我们通过取消组来取消所有组内任务，通过等待组来等待组内所有任务完成，当然我们无法得到任务的结果。

> 它听起来就像是 `Golang` 中 `WaitGroup` 的变异体。

```c++
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

> 往任务组中添加了任务之后，就必须要等待它。

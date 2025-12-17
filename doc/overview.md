# Overview

## Task

`Task` is the core component of `asyncio`:

```c++
asyncio::task::Task<void> test1() {
    while (true) {
        if (co_await task::cancelled)
            throw std::system_error{task::Error::CANCELLED};

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

> Tasks support two methods of error handling: exceptions and error codes. You can choose the one you prefer.

The underlying implementation of a task is based on a state machine using stackless coroutines. Once a task is started, it does not complete immediately; it may need to suspend and resume multiple times. When it finally completes, its associated `promise` is immediately `resolved`. We can wait for one task in another; for example, `asyncio::sleep(1s)` is a task that completes after 1 second. When `co_await asyncio::sleep(1s)` is executed, the current task notices that the `sleep` task was created but has not yet completed, so it suspends itself and hands over control. After `sleep` completes, the result is fetched and the task resumes:

```c++
auto task = asyncio::sleep(1s);

if (!task.done) {
    task.future().then([handle = currentTaskHandle] {
        handle.resume();
    });
    suspend();
}

// Task is completed, continue execution.
tag:
    fmt::print("hello world\n");
```

This is roughly how it works. After suspending itself, the task binds a callback to the `future` of `sleep`. Once the `sleep` task finishes and its `promise` is resolved, the callback function bound to the `future` is executed.

> When `handle.resume()` is executed, it immediately jumps to the `tag` label and continues from there.

## Event Loop

You might be wondering what the internal workings of the `sleep` task are. How does it complete after 1 second, and what state is it in during that time? Before uncovering this, we need to understand some background. `asyncio` uses `libuv`'s event loop at its core. After the main thread creates the event loop, it starts the top-level task and blocks on `uv_run` until the task completes:

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
        const auto eventLoop = EventLoop::make().transform([](EventLoop &&value) {
            return std::make_shared<EventLoop>(std::move(value));
        });
        Z_EXPECT(eventLoop);

        setEventLoop(*eventLoop);

        auto future = f().future().finally([&] {
            eventLoop.value()->stop();
        });

        eventLoop.value()->run();
        assert(future.isReady());
        return {std::move(future).result()};
    }
}

void asyncio::EventLoop::run() {
    uv_run(mLoop.get(), UV_RUN_DEFAULT);
}
```

> After the top-level task completes, the callback function bound to the `future` is called, and the event loop is stopped.

If you've ever used `libuv`, you might have guessed that `sleep` internally uses `uv_timer`:

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

`uv_timer_t` is a timer component from `libuv`. We create it and set the delay, then bind a callback function. Once the time is up, the callback is executed. `sleep` is, of course, a task, so it also needs to be suspended until the deadline is reached. After the deadline, it needs to be resumed. We use a manually constructed `promise` as a bridge for this, with `sleep` waiting for this `promise` to complete, and the callback function resolving the `promise`. Thus, during the 1-second duration, the `sleep` task is suspended while waiting for the `promise`. When the time expires and the `promise` is resolved, execution resumes. This resumption also means the task is completed, and the task that was waiting for it will wake up gradually.

## Cancellation

What if a program starts a `sleep(1h)` task but then encounters an error and needs to exit? To exit gracefully, it's essential to wait for all child tasks to complete. Of course, we don't want to wait for an hour needlessly. Can the task be cancelled? Yes, it can. Most coroutine libraries use `context` to propagate cancellation signals:

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

However, as the project's name suggests, I've chosen a different approach:

```c++
auto task = asyncio::sleep(1h);
task.cancel();
```

> `task.cancel()` is the cancellation method used in Python3 asynchronous tasks. I prefer this simple and direct approach.

A task waits for the completion of another task, and they are chained together like a linked list. When you call `cancel` at the top, it traverses the chain to the end and performs the actual cancellation:

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

If you've read carefully, you'll have noticed the cancellation function registered in `sleep`. `uv_timer_stop` stops the timer, and `promise.reject(task::Error::CANCELLED)` makes the `promise` return a cancellation error.

> Why check `promise.isFulfilled()`? Because after a `promise` is fulfilled, the callback won't be triggered immediately; it waits for the next event loop, and during this time, cancellation can no longer occur.
> This shows that cancellations don't always succeed. If an error occurs, `task.cancel()` will return a `std::error_code`.

Once the cancellation is successful, the `CANCELLED` error is returned from `sleep`. The `Z_CO_EXPECT(sleep(1s))` statement at the higher level will propagate the error up, layer by layer, until it is handled.

Most coroutine libraries prefer `context` for finer-grained control over tasks because, in practice, tasks aren't usually simple chains. They're more like branching trees. When `context` is passed to numerous complex sub-tasks, cancellation initiated at the top can flow down to every branch. Does this mean `task.cancel()` can't provide fine-grained control? Can it only cancel a single task chain?

## Task Tree

When writing a complex program, the task structure is never simple or linear. We may need to wait for multiple sub-tasks to finish, either all of them successfully or any one of them. Inspired by JavaScript's `Promise`, `asyncio` allows us to wait for multiple sub-tasks in the same way:

```c++
// test1 and test2 can have different value types, but must have the same error type.
co_await asyncio::task::all(test1(), test2(), ...);
co_await asyncio::task::allSettled(test1(), test2(), ...);
co_await asyncio::task::any(test1(), test2(), ...);
co_await asyncio::task::race(test1(), test2(), ...);

// test1 and test2 must have the same result type.
co_await asyncio::task::all(std::vector{test1(), test2()});
co_await asyncio::task::allSettled(std::vector{test1(), test2()});
co_await asyncio::task::any(std::vector{test1(), test2()});
co_await asyncio::task::race(std::vector{test1(), test2()});
```

- all: All tasks must succeed; if any one fails, the result fails, and remaining tasks are cancelled.
- allSettled: Wait for all tasks to complete, never fails.
- any: Any task succeeds; remaining tasks are cancelled.
- race: The result is based on the fastest task, and other tasks are cancelled.

> When these functions return, all sub-tasks are guaranteed to have finished.
> More detailed explanations will be added in later chapters and will not be discussed here.

These aggregation operations merge multiple sub-tasks into one, making the task structure resemble a tree, constantly branching out from the top. What we're waiting for at the top is essentially a subtree, and cancelling it means cancelling all of its branches.

```c++
auto task = asyncio::task::all(test1(), test2(), ...);
task.cancel();
```

After aggregation, `asyncio` quietly keeps track of this at the lower level, but users don't need to worry about these details. When `task.cancel()` is called, `test1`, `test2`, and all their sub-tasks will be cancelled.

```text
task
├── test1
│   ├── test3
│   └── test4
└── test2
   

 ├── test5
    └── test6
```

The cancellation operation will be applied to each leaf node of the subtree (3, 4, 5, 7).

> Cancellation on each leaf node may fail. If it fails, `task.cancel()` will return the last error encountered.

## Task Group

How are sub-tasks aggregated into one task? How are sub-tasks managed?  
Before answering this question, let's look at an example:

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
    Z_CO_EXPECT(signal);

    co_return co_await race(
        serve(*std::move(listener)),
        signal->on(SIGINT).transform([](const int) {
        })
    );
}
```

The above example demonstrates a simple `TCP` server. In the `serve` sub-task, it continuously calls `accept` to receive new connections, and then starts a `handle` task to process the connection.  
After starting `handle`, we do not wait for it because we need to continuously call `accept` and cannot block on it. Therefore, we will not know if the `handle` task returns an error, so we bind a callback function to the task's `future` to print any errors.

When the terminal presses `ctrl + c`, the sub-task listening for signals in `race` will complete, at which point `race` will return successfully and cancel the `serve` sub-task. The program will exit and return from the main function.  
While it seems perfect, the downside is that when the main function exits, there may still be N `handle` tasks running. In most cases, this is not an issue, but if `handle` uses some resource that the program destroys during exit, it could lead to unexpected errors.  
We certainly do not want this to happen. The most ideal exit might be to cancel all `handle` sub-tasks and wait for them to complete before `serve` finishes. However, the number of `handle` tasks is unknown; it may be few or many, and more importantly, it is dynamically changing and created in real-time. We cannot manage them using aggregation functions like `all`. So, what should we do?

To achieve this goal, I introduced the concept of a `Task Group`, which is also the underlying component used by operations like `all`.  
We can dynamically add any number of tasks to a task group, and the tasks will be automatically removed from the group once completed. We cancel all tasks in the group by canceling the group itself, and we wait for all tasks in the group to complete by awaiting the group, but we cannot obtain the result of the tasks.

> It sounds like a variant of `Golang`'s `WaitGroup`.

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

> After adding tasks to the task group, we must wait for them.

# Channel

This module provides `channel` functionality for concurrent communication between tasks and threads.

## Function `channel`

```cpp
template<typename T>
using Channel = std::pair<Sender<T>, Receiver<T>>;

template<typename T>
Channel<T> channel(std::shared_ptr<EventLoop> eventLoop, const std::size_t capacity);

template<typename T>
Channel<T> channel(const std::size_t capacity);
```

Creates a fixed-capacity `channel`, bound to the current thread's `Event Loop` by default, or manually specified.

> The `channel` uses a ring buffer internally, and the capacity must be greater than 1.

```cpp
auto [sender, receiver] = asyncio::channel<std::string>(100);

REQUIRE(co_await sender.send("hello world"));
REQUIRE(co_await receiver.receive() == "hello world");
```

## Class `Sender`

`Sender` is the sending end, which can be moved and copied with an internal reference count. When all `Sender` instances are destroyed, the `channel` will automatically close.

### Method `trySend`

```cpp
DEFINE_ERROR_CODE_EX(
    TrySendError,
    "asyncio::Sender::trySend",
    DISCONNECTED, "sending on a disconnected channel", DEFAULT_ERROR_CONDITION,
    FULL, "sending on a full channel", std::errc::operation_would_block
)

template<typename U = T>
std::expected<void, TrySendError> trySend(U &&element);
```

Attempts to send data without waiting when the `channel` is full, returning a `TrySendError::FULL` error. Returns `TrySendError::DISCONNECTED` when the `channel` is closed.

> This function can be used from any thread.

### Method `sendSync`

```cpp
DEFINE_ERROR_CODE_EX(
    SendSyncError,
    "asyncio::Sender::sendSync",
    DISCONNECTED, "sending on a disconnected channel", DEFAULT_ERROR_CONDITION,
    TIMEOUT, "timed out waiting on send operation", std::errc::timed_out
)

template<typename U = T>
std::expected<void, SendSyncError>
sendSync(U &&element, const std::optional<std::chrono::milliseconds> timeout = std::nullopt);
```

Synchronously sends data, blocking the current thread when the `channel` is full, returning after successful send, timeout, or `channel` closure.

> When the `timeout` parameter is not set, `sendSync` will block indefinitely until successful send or `channel` closure.

```cpp
auto [sender, receiver] = asyncio::channel<std::string>(100);

co_await asyncio::toThread([&] {
    using namespace std::chrono_literals;

    sender.sendSync("hello world");
    sender.sendSync("hello world", 1s);
});
```

> Do not call `sendSync` in the `Event Loop` main thread!

### Method `send`

```cpp
DEFINE_ERROR_CODE_EX(
    SendError,
    "asyncio::Sender::send",
    DISCONNECTED, "sending on a disconnected channel", DEFAULT_ERROR_CONDITION,
    CANCELLED, "send operation has been cancelled", std::errc::operation_canceled
)

task::Task<void, SendError> send(T element);
```

Asynchronously sends data, suspending when the `channel` is full until successful send or `channel` closure.

> `send` can only be called from within the `Event Loop` main thread.

```cpp
using namespace std::chrono_literals;

auto [sender, receiver] = asyncio::channel<std::string>(100);

co_await sender.send("hello world");
co_await timeout(sender.send("hello world"), 1s);
```

> `send` does not provide a timeout parameter. Like all async functions in `asyncio`, timeout control should use `asyncio::timeout`.

### Method `close`

```cpp
void close();
```

Closes the `channel`, waking all waiting senders and receivers. After closure, send operations will always fail and return a `DISCONNECTED` error.

> Repeated closures have no effect.

### Method `size`

```cpp
[[nodiscard]] std::size_t size() const;
```

Gets the number of elements stored in the `channel`.

### Method `capacity`

```cpp
[[nodiscard]] std::size_t capacity() const;
```

Gets the capacity of the `channel`.

> The maximum number of elements that can be stored in the `channel` is `capacity - 1`.

### Method `empty`

```cpp
[[nodiscard]] bool empty() const;
```

Checks if the `channel` is empty.

### Method `full`

```cpp
[[nodiscard]] bool full() const;
```

Checks if the `channel` is full.

### Method `closed`

```cpp
[[nodiscard]] bool closed() const;
```

Checks if the `channel` has been closed.

## Class `Receiver`

`Receiver` is the receiving end, which can be moved and copied with an internal reference count. When all `Receiver` instances are destroyed, the `channel` will automatically close.

### Method `tryReceive`


```cpp
DEFINE_ERROR_CODE_EX(
    TryReceiveError,
    "asyncio::Receiver::tryReceive",
    DISCONNECTED, "receiving on an empty and disconnected channel", DEFAULT_ERROR_CONDITION,
    EMPTY, "receiving on an empty channel", std::errc::operation_would_block
)

std::expected<T, TryReceiveError> tryReceive();
```

Attempts to receive data without waiting when the `channel` is empty, returning a `TryReceiveError::EMPTY` error. Returns `TryReceiveError::DISCONNECTED` when the `channel` is closed.

> This function can be used from any thread.

### Method `receiveSync`

```cpp
DEFINE_ERROR_CODE_EX(
    ReceiveSyncError,
    "asyncio::Receiver::receiveSync",
    DISCONNECTED, "channel is empty and disconnected", DEFAULT_ERROR_CONDITION,
    TIMEOUT, "timed out waiting on receive operation", std::errc::timed_out
)

std::expected<T, ReceiveSyncError>
receiveSync(const std::optional<std::chrono::milliseconds> timeout = std::nullopt);
```

Synchronously receives data, blocking the current thread when the `channel` is empty, returning after successful receive, timeout, or `channel` closure.

> When the `timeout` parameter is not set, `receiveSync` will block indefinitely until successful receive or `channel` closure.

```cpp
auto [sender, receiver] = asyncio::channel<std::string>(100);

co_await asyncio::toThread([&] {
    using namespace std::chrono_literals;

    receiver.receiveSync();
    receiver.receiveSync(1s);
});
```

> Do not call `receiveSync` in the `Event Loop` main thread!

### Method `receive`

```cpp
DEFINE_ERROR_CODE_EX(
    ReceiveError,
    "asyncio::Receiver::receive",
    DISCONNECTED, "channel is empty and disconnected", DEFAULT_ERROR_CONDITION,
    CANCELLED, "receive operation has been cancelled", std::errc::operation_canceled
)

task::Task<T, ReceiveError> receive();
```

Asynchronously receives data, suspending when the `channel` is empty until successful receive or `channel` closure.

> `receive` can only be called from within the `Event Loop` main thread.

```cpp
using namespace std::chrono_literals;

auto [sender, receiver] = asyncio::channel<std::string>(100);

co_await receiver.receive();
co_await timeout(receiver.receive(), 1s);
```

> `receive` does not provide a timeout parameter. Like all async functions in `asyncio`, timeout control should use `asyncio::timeout`.

### Method `close`

```cpp
void close();
```

Closes the `channel`, waking all waiting senders and receivers. If there is still data remaining in the `channel` after closure, the receiver can still read it, returning a `DISCONNECTED` error only after all data is consumed.

> Repeated closures have no effect.

### Method `size`

```cpp
[[nodiscard]] std::size_t size() const;
```

Gets the number of elements stored in the `channel`.

### Method `capacity`

```cpp
[[nodiscard]] std::size_t capacity() const;
```

Gets the capacity of the `channel`.

> The maximum number of elements that can be stored in the `channel` is `capacity - 1`.

### Method `empty`

```cpp
[[nodiscard]] bool empty() const;
```

Checks if the `channel` is empty.

### Method `full`

```cpp
[[nodiscard]] bool full() const;
```

Checks if the `channel` is full.

### Method `closed`

```cpp
[[nodiscard]] bool closed() const;
```

Checks if the `channel` has been closed.

## Error Condition `ChannelError`

```cpp
DEFINE_ERROR_CONDITION_EX(
    ChannelError,
    "asyncio::channel",
    DISCONNECTED,
    "channel disconnected",
    [](const std::error_code &ec) {
        return ec == make_error_code(TrySendError::DISCONNECTED) ||
            ec == make_error_code(SendSyncError::DISCONNECTED) ||
            ec == make_error_code(SendError::DISCONNECTED) ||
            ec == make_error_code(TryReceiveError::DISCONNECTED) ||
            ec == make_error_code(ReceiveSyncError::DISCONNECTED) ||
            ec == make_error_code(ReceiveError::DISCONNECTED);
    }
)
```

The `channel`-related `error condition`, `ChannelError::DISCONNECTED` can be used to determine if an error is caused by `channel` closure.

```cpp
const auto result = co_await func();

if (!result) {
    if (result.error() == ChannelError::DISCONNECTED) {
        // ...
    }
}
```

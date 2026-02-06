# Channel

This module provides `channel`-related functionality for concurrent communication between tasks and threads.

## Function `channel`

```c++
template<typename T>
using Channel = std::pair<Sender<T>, Receiver<T>>;

template<typename T>
Channel<T> channel(std::shared_ptr<EventLoop> eventLoop, const std::size_t capacity = 1);

template<typename T>
Channel<T> channel(const std::size_t capacity = 1);
```

Creates a fixed-capacity `channel`, bound to the current thread's `Event Loop` by default, or can be manually specified.

```c++
auto [sender, receiver] = asyncio::channel<std::string>(100);

REQUIRE(co_await sender.send("hello world"));
REQUIRE(co_await receiver.receive() == "hello world");
```

## Class `Sender`

`Sender` is the sending end, can be moved and copied, holds a reference count internally. When all `Sender` instances are destroyed, the `channel` will automatically close.

### Method `trySend`

```c++
Z_DEFINE_ERROR_CODE_EX(
    TrySendError,
    "asyncio::Sender::trySend",
    Disconnected, "Sending on a disconnected channel", Z_DEFAULT_ERROR_CONDITION,
    Full, "Sending on a full channel", std::errc::operation_would_block
)

template<typename U = T>
std::expected<void, TrySendError> trySend(U &&element);
```

Attempts to send data without waiting when the `channel` is full, returning a `TrySendError::Full` error. Returns `TrySendError::Disconnected` when the `channel` is closed.

> This function can be used from any thread.

### Method `sendSync`

```c++
Z_DEFINE_ERROR_CODE_EX(
    SendSyncError,
    "asyncio::Sender::sendSync",
    Disconnected, "Sending on a disconnected channel", Z_DEFAULT_ERROR_CONDITION,
    Timeout, "Send operation timed out", std::errc::timed_out
)

template<typename U = T>
std::expected<void, SendSyncError>
sendSync(U &&element, const std::optional<std::chrono::milliseconds> timeout = std::nullopt);
```

Synchronously sends data, blocking the current thread when the `channel` is full, returning after successful send, timeout, or `channel` closure.

> When the `timeout` parameter is not set, `sendSync` will block indefinitely until successful send or `channel` closure.

```c++
auto [sender, receiver] = asyncio::channel<std::string>(100);

co_await asyncio::toThread([&] {
    using namespace std::chrono_literals;

    sender.sendSync("hello world");
    sender.sendSync("hello world", 1s);
});
```

> Do not call `sendSync` in the `Event Loop` main thread!

### Method `send`

```c++
Z_DEFINE_ERROR_CODE_EX(
    SendError,
    "asyncio::Sender::send",
    Disconnected, "Sending on a disconnected channel", Z_DEFAULT_ERROR_CONDITION,
    Cancelled, "Send operation was cancelled", std::errc::operation_canceled
)

task::Task<void, SendError> send(T element);
```

Asynchronously sends data, suspending when the `channel` is full until successful send or `channel` closure.

> `send` can only be called from within the `Event Loop` main thread.

```c++
using namespace std::chrono_literals;

auto [sender, receiver] = asyncio::channel<std::string>(100);

co_await sender.send("hello world");
co_await timeout(sender.send("hello world"), 1s);
```

> `send` does not provide a timeout parameter. All async functions in `asyncio` follow this pattern. For timeout control, use `asyncio::timeout`.

### Method `close`

```c++
void close();
```

Closes the `channel`, waking up all waiting senders and receivers. After the `channel` is closed, send operations will always fail, returning a `Disconnected` error.

> Repeated closures have no effect.

### Method `size`

```c++
[[nodiscard]] std::size_t size() const;
```

Gets the number of elements stored in the `channel`.

### Method `capacity`

```c++
[[nodiscard]] std::size_t capacity() const;
```

Gets the capacity of the `channel`.

> The maximum number of elements that can be stored in the `channel` is `capacity - 1`;

### Method `empty`

```c++
[[nodiscard]] bool empty() const;
```

Checks if the `channel` is empty.

### Method `full`

```c++
[[nodiscard]] bool full() const;
```

Checks if the `channel` is full.

### Method `closed`

```c++
[[nodiscard]] bool closed() const;
```

Checks if the `channel` has been closed.

## Class `Receiver`

`Receiver` is the receiving end, can be moved and copied, holds a reference count internally. When all `Receiver` instances are destroyed, the `channel` will automatically close.

### Method `tryReceive`


```c++
Z_DEFINE_ERROR_CODE_EX(
    TryReceiveError,
    "asyncio::Receiver::tryReceive",
    Disconnected, "Receiving on an empty and disconnected channel", Z_DEFAULT_ERROR_CONDITION,
    Empty, "Receiving on an empty channel", std::errc::operation_would_block
)

std::expected<T, TryReceiveError> tryReceive();
```

Attempts to receive data without waiting when the `channel` is empty, returning a `TryReceiveError::Empty` error. Returns `TryReceiveError::Disconnected` when the `channel` is closed.

> This function can be used from any thread.

### Method `receiveSync`

```c++
Z_DEFINE_ERROR_CODE_EX(
    ReceiveSyncError,
    "asyncio::Receiver::receiveSync",
    Disconnected, "Receiving on an empty and disconnected channel", Z_DEFAULT_ERROR_CONDITION,
    Timeout, "Receive operation timed out", std::errc::timed_out
)

std::expected<T, ReceiveSyncError>
receiveSync(const std::optional<std::chrono::milliseconds> timeout = std::nullopt);
```

Synchronously receives data, blocking the current thread when the `channel` is empty, returning after successful receive, timeout, or `channel` closure.

> When the `timeout` parameter is not set, `receiveSync` will block indefinitely until successful receive or `channel` closure.

```c++
auto [sender, receiver] = asyncio::channel<std::string>(100);

co_await asyncio::toThread([&] {
    using namespace std::chrono_literals;

    receiver.receiveSync();
    receiver.receiveSync(1s);
});
```

> Do not call `receiveSync` in the `Event Loop` main thread!

### Method `receive`

```c++
Z_DEFINE_ERROR_CODE_EX(
    ReceiveError,
    "asyncio::Receiver::receive",
    Disconnected, "Receiving on an empty and disconnected channel", Z_DEFAULT_ERROR_CONDITION,
    Cancelled, "Receive operation was cancelled", std::errc::operation_canceled
)

task::Task<T, ReceiveError> receive();
```

Asynchronously receives data, suspending when the `channel` is empty until successful receive or `channel` closure.

> `receive` can only be called from within the `Event Loop` main thread.

```c++
using namespace std::chrono_literals;

auto [sender, receiver] = asyncio::channel<std::string>(100);

co_await receiver.receive();
co_await timeout(receiver.receive(), 1s);
```

> `receive` does not provide a timeout parameter. All async functions in `asyncio` follow this pattern. For timeout control, use `asyncio::timeout`.

### Method `close`

```c++
void close();
```

Closes the `channel`, waking up all waiting senders and receivers. If data remains in the `channel` after closing, the receiver can still read it. Only after all data is consumed will it return a `Disconnected` error.

> Repeated closures have no effect.

### Method `size`

```c++
[[nodiscard]] std::size_t size() const;
```

Gets the number of elements stored in the `channel`.

### Method `capacity`

```c++
[[nodiscard]] std::size_t capacity() const;
```

Gets the capacity of the `channel`.

> The maximum number of elements that can be stored in the `channel` is `capacity - 1`;

### Method `empty`

```c++
[[nodiscard]] bool empty() const;
```

Checks if the `channel` is empty.

### Method `full`

```c++
[[nodiscard]] bool full() const;
```

Checks if the `channel` is full.

### Method `closed`

```c++
[[nodiscard]] bool closed() const;
```

Checks if the `channel` has been closed.

## Error Condition `ChannelError`

```c++
Z_DEFINE_ERROR_CONDITION_EX(
    ChannelError,
    "asyncio::channel",
    Disconnected,
    "Channel disconnected",
    [](const std::error_code &ec) {
        return ec == make_error_code(TrySendError::Disconnected) ||
            ec == make_error_code(SendSyncError::Disconnected) ||
            ec == make_error_code(SendError::Disconnected) ||
            ec == make_error_code(TryReceiveError::Disconnected) ||
            ec == make_error_code(ReceiveSyncError::Disconnected) ||
            ec == make_error_code(ReceiveError::Disconnected);
    }
)
```

`error condition` related to `channel`, `ChannelError::Disconnected` can be used to determine if an error is caused by `channel` closure.

```c++
const auto result = co_await func();

if (!result) {
    if (result.error() == ChannelError::Disconnected) {
        // ...
    }
}
```

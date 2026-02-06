# Channel

此模块提供 `channel` 相关的功能，用于在任务、线程之间进行并发通信。

## Function `channel`

```c++
template<typename T>
using Channel = std::pair<Sender<T>, Receiver<T>>;

template<typename T>
Channel<T> channel(std::shared_ptr<EventLoop> eventLoop, const std::size_t capacity = 1);

template<typename T>
Channel<T> channel(const std::size_t capacity = 1);
```

创建固定容量的 `channel`，默认绑定到当前线程的 `Event Loop`，也可以手动指定。

```c++
auto [sender, receiver] = asyncio::channel<std::string>(100);

REQUIRE(co_await sender.send("hello world"));
REQUIRE(co_await receiver.receive() == "hello world");
```

## Class `Sender`

`Sender` 为发送端，可以移动和复制，内部持有引用计数，当所有 `Sender` 销毁后，`channel` 将自动关闭。

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

尝试发送数据，当 `channel` 已满时不等待，返回 `TrySendError::Full` 错误，当 `channel` 已关闭时返回 `TrySendError::Disconnected`。

> 此函数可在任意线程中使用。

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

同步发送数据，当 `channel` 已满时会阻塞当前线程，发送成功、等待超时或 `channel` 被关闭后返回。

> 未设置 `timeout` 参数时，`sendSync` 将会永久阻塞直到发送成功或 `channel` 被关闭。

```c++
auto [sender, receiver] = asyncio::channel<std::string>(100);

co_await asyncio::toThread([&] {
    using namespace std::chrono_literals;

    sender.sendSync("hello world");
    sender.sendSync("hello world", 1s);
});
```

> 不要在 `Event Loop` 主线程中调用 `sendSync`！

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

异步发送数据，当 `channel` 已满时会挂起，直到发送成功或 `channel` 被关闭。

> `send` 只能在 `Event Loop` 主线程内调用。

```c++
using namespace std::chrono_literals;

auto [sender, receiver] = asyncio::channel<std::string>(100);

co_await sender.send("hello world");
co_await timeout(sender.send("hello world"), 1s);
```

> `send` 不提供设置超时的参数，`asyncio` 中所有的异步函数皆是如此，超时控制请使用 `asyncio::timeout`。

### Method `close`

```c++
void close();
```

关闭 `channel`，这将唤醒所有发送端与接收端的等待者。`channel` 关闭后，发送操作将永远失败，返回 `Disconnected` 错误。

> 重复关闭无任何影响。

### Method `size`

```c++
[[nodiscard]] std::size_t size() const;
```

获取 `channel` 内储存的元素数量。

### Method `capacity`

```c++
[[nodiscard]] std::size_t capacity() const;
```

获取 `channel` 的容量。

> `channel` 最大可储存的元素数量为 `capacity - 1`;

### Method `empty`

```c++
[[nodiscard]] bool empty() const;
```

检查 `channel` 是否为空。

### Method `full`

```c++
[[nodiscard]] bool full() const;
```

检查 `channel` 是否已满。

### Method `closed`

```c++
[[nodiscard]] bool closed() const;
```

检查 `channel` 是否已被关闭。

## Class `Receiver`

`Receiver` 为接收端，可以移动和复制，内部持有引用计数，当所有 `Receiver` 销毁后，`channel` 将自动关闭。

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

尝试接收数据，当 `channel` 为空时不等待，返回 `TryReceiveError::Empty` 错误，当 `channel` 已关闭时返回 `TryReceiveError::Disconnected`。

> 此函数可在任意线程中使用。

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

同步接收数据，当 `channel` 为空时会阻塞当前线程，接收成功、等待超时或 `channel` 被关闭后返回。

> 未设置 `timeout` 参数时，`receiveSync` 将会永久阻塞直到接收成功或 `channel` 被关闭。

```c++
auto [sender, receiver] = asyncio::channel<std::string>(100);

co_await asyncio::toThread([&] {
    using namespace std::chrono_literals;

    receiver.receiveSync();
    receiver.receiveSync(1s);
});
```

> 不要在 `Event Loop` 主线程中调用 `receiveSync`！

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

异步接收数据，当 `channel` 为空时会挂起，直到接收成功或 `channel` 被关闭。

> `receive` 只能在 `Event Loop` 主线程内调用。

```c++
using namespace std::chrono_literals;

auto [sender, receiver] = asyncio::channel<std::string>(100);

co_await receiver.receive();
co_await timeout(receiver.receive(), 1s);
```

> `receive` 不提供设置超时的参数，`asyncio` 中所有的异步函数皆是如此，超时控制请使用 `asyncio::timeout`。

### Method `close`

```c++
void close();
```

关闭 `channel`，这将唤醒所有发送端与接收端的等待者。如果关闭后 `channel` 内还残留有数据，接收端依旧可以读取，在数据消费完后才会返回 `Disconnected` 错误。

> 重复关闭无任何影响。

### Method `size`

```c++
[[nodiscard]] std::size_t size() const;
```

获取 `channel` 内储存的元素数量。

### Method `capacity`

```c++
[[nodiscard]] std::size_t capacity() const;
```

获取 `channel` 的容量。

> `channel` 最大可储存的元素数量为 `capacity - 1`;

### Method `empty`

```c++
[[nodiscard]] bool empty() const;
```

检查 `channel` 是否为空。

### Method `full`

```c++
[[nodiscard]] bool full() const;
```

检查 `channel` 是否已满。

### Method `closed`

```c++
[[nodiscard]] bool closed() const;
```

检查 `channel` 是否已被关闭。

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

`channel` 相关的 `error condition`，`ChannelError::Disconnected` 可用于判断出错误是否是 `channel` 关闭导致的。

```c++
const auto result = co_await func();

if (!result) {
    if (result.error() == ChannelError::Disconnected) {
        // ...
    }
}
```
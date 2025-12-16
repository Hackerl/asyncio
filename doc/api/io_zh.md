# IO

此模块提供统一的异步 `IO` 接口定义。

## Error Condition `IOError`

```cpp
Z_DEFINE_ERROR_CONDITION(
    IOError,
    "asyncio::io",
    UNEXPECTED_EOF, "unexpected end of file"
)
```

`IO` 接口通用的 `error condition`，现在只有 `UNEXPECTED_EOF` 这一种错误。

## Type `FileDescriptor`

```
using FileDescriptor = uv_os_fd_t;
```

在 `Unix` 系统中，`FileDescriptor` 是 `int` 类型，在 `Windows` 系统中，`FileDescriptor` 是 `HANDLE` 类型。

## Interface `IFileDescriptor`

```cpp
class IFileDescriptor : public virtual zero::Interface {
public:
    [[nodiscard]] virtual FileDescriptor fd() const = 0;
};
```

用于获取底层文件描述符。

## Interface `ICloseable`

```cpp
class ICloseable : public virtual zero::Interface {
public:
    virtual task::Task<void, std::error_code> close() = 0;
};
```

用于关闭底层资源。

### Interface `IHalfCloseable`

```cpp
class IHalfCloseable : public virtual zero::Interface {
public:
    virtual task::Task<void, std::error_code> shutdown() = 0;
};
```

关闭 `IO` 的写端，保留读端，只有少数几类连接支持此操作。

```cpp
class IReader : public virtual zero::Interface {
public:
    Z_DEFINE_ERROR_CODE_INNER_EX(
        ReadExactlyError,
        "asyncio::IReader",
        UNEXPECTED_EOF, "unexpected end of file", make_error_condition(IOError::UNEXPECTED_EOF)
    )

    virtual task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) = 0;
    virtual task::Task<void, std::error_code> readExactly(std::span<std::byte> data);
    virtual task::Task<std::vector<std::byte>, std::error_code> readAll();
};
```

所有支持读取操作的 `IO` 资源都实现了 `IReader` 接口。

### Method `read`

```cpp
virtual task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) = 0;
```

读取数据到 `data` 中，返回实际读取的字节数，实际读取的字节数可能小于 `data` 的长度；如果读取到文件末尾，则返回 `0`。

### Method `readExactly`

```cpp
virtual task::Task<void, std::error_code> readExactly(std::span<std::byte> data);
```

读取精确数量的数据填满 `data`，如果数据不足，则返回 `ReadExactlyError::UNEXPECTED_EOF` 错误。

### Method `readAll`

```cpp
virtual task::Task<std::vector<std::byte>, std::error_code> readAll();
```

读出所有数据，返回一个 `std::vector<std::byte>` 对象。

> 数据量非常大时，请谨慎使用此方法。

## Interface `IWriter`

```cpp
class IWriter : public virtual zero::Interface {
public:
    virtual task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) = 0;
    virtual task::Task<void, std::error_code> writeAll(std::span<const std::byte> data);
};
```

所有支持写入操作的 `IO` 资源都实现了 `IWriter` 接口。

### Method `write`

```cpp
virtual task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) = 0;
```

写入数据，返回实际写入的字节数，实际写入的字节数可能小于 `data` 的长度。

> 如果成功写入了部分数据，但发生了错误，则返回已写入的数据数量。

### Method `writeAll`

```cpp
virtual task::Task<void, std::error_code> writeAll(std::span<const std::byte> data);
```

写入所有数据，如果发生错误，则返回错误。

> 应用层所有的写入操作对推荐使用 `writeAll` 方法。

## Interface `ISeekable`

```cpp
class ISeekable : public virtual zero::Interface {
public:
    enum class Whence {
        BEGIN,
        CURRENT,
        END
    };

    virtual task::Task<std::uint64_t, std::error_code> seek(std::int64_t offset, Whence whence) = 0;
    virtual task::Task<void, std::error_code> rewind();
    virtual task::Task<std::uint64_t, std::error_code> length();
    virtual task::Task<std::uint64_t, std::error_code> position();
};
```

所有支持随机访问的 `IO` 资源都实现了 `ISeekable` 接口。

### Method `seek`

```cpp
virtual task::Task<std::uint64_t, std::error_code> seek(std::int64_t offset, Whence whence) = 0;
```

移动文件指针，返回新的文件指针位置。

### Method `rewind`

```cpp
virtual task::Task<void, std::error_code> rewind();
```

将文件指针移动到文件开头。

### Method `length`

```cpp
virtual task::Task<std::uint64_t, std::error_code> length();
```

获取文件长度。

### Method `position`

```cpp
virtual task::Task<std::uint64_t, std::error_code> position();
```

获取文件指针位置。

## Interface `IBufReader`

```cpp
class IBufReader : public virtual IReader {
public:
    [[nodiscard]] virtual std::size_t available() const = 0;
    virtual task::Task<std::string, std::error_code> readLine() = 0;
    virtual task::Task<std::vector<std::byte>, std::error_code> readUntil(std::byte byte) = 0;
    virtual task::Task<void, std::error_code> peek(std::span<std::byte> data) = 0;
};
```

所有支持缓冲读取操作的 `IO` 资源都实现了 `IBufReader` 接口。

### Method `available`

```cpp
[[nodiscard]] virtual std::size_t available() const = 0;
```

返回缓冲区中可读取的字节数。

### Method `readLine`

```cpp
virtual task::Task<std::string, std::error_code> readLine() = 0;
```

读取一行数据，返回一个 `std::string` 对象。如果读取到换行符之前没有数据了，则返回 `UNEXPECTED_EOF` 错误。

> 支持 `\r\n` 和 `\n` 两种换行符。

### Method `readUntil`

```cpp
virtual task::Task<std::vector<std::byte>, std::error_code> readUntil(std::byte byte) = 0;
```

读取数据直到遇到 `byte` 字节，返回一个 `std::vector<std::byte>` 对象。如果读取到 `byte` 字节之前没有数据了，则返回 `UNEXPECTED_EOF` 错误。

### Method `peek`

```cpp
virtual task::Task<void, std::error_code> peek(std::span<std::byte> data) = 0;
```

预读取数据到 `data` 中，长度不能超过缓冲区大小。如果数据不足，则返回 `UNEXPECTED_EOF` 错误。

## Interface `IBufWriter`

```cpp
class IBufWriter : public virtual IWriter {
public:
    [[nodiscard]] virtual std::size_t pending() const = 0;
    virtual task::Task<void, std::error_code> flush() = 0;
};
```

所有支持缓冲写入操作的 `IO` 资源都实现了 `IBufWriter` 接口。

### Method `pending`

```cpp
[[nodiscard]] virtual std::size_t pending() const = 0;
```

返回缓冲区中未写入的字节数。

### Method `flush`

```cpp
virtual task::Task<void, std::error_code> flush() = 0;
```

将缓冲区中的数据全部写入到底层 `IO` 中。

## Function `copy`

```cpp
task::Task<std::size_t, std::error_code> copy(zero::detail::Trait<IReader> auto &reader, zero::detail::Trait<IWriter> auto &writer);
```

从 `reader` 读取数据，然后写入到 `writer` 中，直到 `read` 返回 `0` 或 `write` 发生了错误，返回实际复制的字节数。

> 默认使用 `20480` 字节的缓冲区用于缓冲数据。

### Class `StringReader`

```cpp
class StringReader final : public IReader {
public:
    explicit StringReader(std::string string);
};
```

将 `std::string` 对象包装成 `IReader` 接口。

### Method `read`

```cpp
task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
```

从字符串中读取数据到 `data` 中。

### Class `StringWriter`

```cpp
class StringWriter final : public IWriter {
public:
    explicit StringWriter(std::string &string);
};
```

将 `std::string` 对象包装成 `IWriter` 接口。

### Method `write`

```cpp
task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;
```

将数据写入到字符串中。

### Method `data`

```cpp
auto &&data(this Self &&self) {
    return self.mString;
}
```

访问底层的 `std::string` 成员。

### Method `operator*`

```cpp
auto &&operator*(this Self &&self) {
    return self.mString;
}
```

访问底层的 `std::string` 成员。

### Class `BytesReader`

```cpp
class BytesReader final : public IReader {
public:
    explicit BytesReader(std::vector<std::byte> bytes);
};
```

将 `std::vector<std::byte>` 对象包装成 `IReader` 接口。

### Method `read`

```cpp
task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
```

从字节数组中读取数据到 `data` 中。

### Class `BytesWriter`

```cpp
class BytesWriter final : public IWriter {
public:
    explicit BytesWriter(std::vector<std::byte> &bytes);
};
```

将 `std::vector<std::byte>` 对象包装成 `IWriter` 接口。

### Method `write`

```cpp
task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;
```

将数据写入到字节数组中。

### Method `data`

```cpp
auto &&data(this Self &&self) {
    return self.mBytes;
}
```

访问底层的 `std::vector<std::byte>` 成员。

### Method `operator*`

```cpp
auto &&operator*(this Self &&self) {
    return self.mBytes;
}
```

访问底层的 `std::vector<std::byte>` 成员。
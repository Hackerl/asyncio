# IO

This module provides unified async `IO` interface definitions.

## Error Condition `IOError`

```cpp
Z_DEFINE_ERROR_CONDITION(
    IOError,
    "asyncio::io",
    UNEXPECTED_EOF, "unexpected end of file"
)
```

Common `error condition` for `IO` interfaces, currently only has the `UNEXPECTED_EOF` error.

## Type `FileDescriptor`

```
using FileDescriptor = uv_os_fd_t;
```

On `Unix` systems, `FileDescriptor` is of type `int`. On `Windows` systems, `FileDescriptor` is of type `HANDLE`.

## Interface `IFileDescriptor`

```cpp
class IFileDescriptor : public virtual zero::Interface {
public:
    [[nodiscard]] virtual FileDescriptor fd() const = 0;
};
```

Used to get the underlying file descriptor.

## Interface `ICloseable`

```cpp
class ICloseable : public virtual zero::Interface {
public:
    virtual task::Task<void, std::error_code> close() = 0;
};
```

Used to close the underlying resource.

### Interface `IHalfCloseable`

```cpp
class IHalfCloseable : public virtual zero::Interface {
public:
    virtual task::Task<void, std::error_code> shutdown() = 0;
};
```

Closes the write end of the `IO` while keeping the read end open. Only a few connection types support this operation.

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

All `IO` resources that support read operations implement the `IReader` interface.

### Method `read`

```cpp
virtual task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) = 0;
```

Reads data into `data`, returning the actual number of bytes read. The actual number of bytes read may be less than the length of `data`. Returns `0` if end of file is reached.

### Method `readExactly`

```cpp
virtual task::Task<void, std::error_code> readExactly(std::span<std::byte> data);
```

Reads exactly enough data to fill `data`. If insufficient data is available, returns a `ReadExactlyError::UNEXPECTED_EOF` error.

### Method `readAll`

```cpp
virtual task::Task<std::vector<std::byte>, std::error_code> readAll();
```

Reads all data and returns a `std::vector<std::byte>` object.

> Use this method with caution when dealing with very large amounts of data.

## Interface `IWriter`

```cpp
class IWriter : public virtual zero::Interface {
public:
    virtual task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) = 0;
    virtual task::Task<void, std::error_code> writeAll(std::span<const std::byte> data);
};
```

All `IO` resources that support write operations implement the `IWriter` interface.

### Method `write`

```cpp
virtual task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) = 0;
```

Writes data and returns the actual number of bytes written. The actual number of bytes written may be less than the length of `data`.

> If some data was successfully written but an error occurred, returns the amount of data that was written.

### Method `writeAll`

```cpp
virtual task::Task<void, std::error_code> writeAll(std::span<const std::byte> data);
```

Writes all data. If an error occurs, returns the error.

> All application-layer write operations should use the `writeAll` method.

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

All `IO` resources that support random access implement the `ISeekable` interface.

### Method `seek`

```cpp
virtual task::Task<std::uint64_t, std::error_code> seek(std::int64_t offset, Whence whence) = 0;
```

Moves the file pointer and returns the new file pointer position.

### Method `rewind`

```cpp
virtual task::Task<void, std::error_code> rewind();
```

Moves the file pointer to the beginning of the file.

### Method `length`

```cpp
virtual task::Task<std::uint64_t, std::error_code> length();
```

Gets the file length.

### Method `position`

```cpp
virtual task::Task<std::uint64_t, std::error_code> position();
```

Gets the file pointer position.

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

All `IO` resources that support buffered read operations implement the `IBufReader` interface.

### Method `available`

```cpp
[[nodiscard]] virtual std::size_t available() const = 0;
```

Returns the number of bytes available to read in the buffer.

### Method `readLine`

```cpp
virtual task::Task<std::string, std::error_code> readLine() = 0;
```

Reads a line of data and returns a `std::string` object. If no data is available before a newline character, returns an `UNEXPECTED_EOF` error.

> Supports both `\r\n` and `\n` line endings.

### Method `readUntil`

```cpp
virtual task::Task<std::vector<std::byte>, std::error_code> readUntil(std::byte byte) = 0;
```

Reads data until encountering the `byte` and returns a `std::vector<std::byte>` object. If no data is available before the `byte`, returns an `UNEXPECTED_EOF` error.

### Method `peek`

```cpp
virtual task::Task<void, std::error_code> peek(std::span<std::byte> data) = 0;
```

Pre-reads data into `data`, with length not exceeding the buffer size. If insufficient data is available, returns an `UNEXPECTED_EOF` error.

## Interface `IBufWriter`

```cpp
class IBufWriter : public virtual IWriter {
public:
    [[nodiscard]] virtual std::size_t pending() const = 0;
    virtual task::Task<void, std::error_code> flush() = 0;
};
```

All `IO` resources that support buffered write operations implement the `IBufWriter` interface.

### Method `pending`

```cpp
[[nodiscard]] virtual std::size_t pending() const = 0;
```

Returns the number of bytes pending in the buffer.

### Method `flush`

```cpp
virtual task::Task<void, std::error_code> flush() = 0;
```

Writes all data in the buffer to the underlying `IO`.

## Function `copy`

```cpp
task::Task<std::size_t, std::error_code> copy(zero::detail::Trait<IReader> auto &reader, zero::detail::Trait<IWriter> auto &writer);
```

Reads data from `reader` and writes it to `writer` until `read` returns `0` or `write` encounters an error. Returns the actual number of bytes copied.

> Uses a `20480` byte buffer by default for buffering data.

### Class `StringReader`

```cpp
class StringReader final : public IReader {
public:
    explicit StringReader(std::string string);
};
```

Wraps a `std::string` object as an `IReader` interface.

### Method `read`

```cpp
task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
```

Reads data from the string into `data`.

### Class `StringWriter`

```cpp
class StringWriter final : public IWriter {
public:
    explicit StringWriter(std::string &string);
};
```

Wraps a `std::string` object as an `IWriter` interface.

### Method `write`

```cpp
task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;
```

Writes data into the string.

### Method `data`

```cpp
auto &&data(this Self &&self) {
    return self.mString;
}
```

Accesses the underlying `std::string` member.

### Method `operator*`

```cpp
auto &&operator*(this Self &&self) {
    return self.mString;
}
```

Accesses the underlying `std::string` member.

### Class `BytesReader`

```cpp
class BytesReader final : public IReader {
public:
    explicit BytesReader(std::vector<std::byte> bytes);
};
```

Wraps a `std::vector<std::byte>` object as an `IReader` interface.

### Method `read`

```cpp
task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
```

Reads data from the byte array into `data`.

### Class `BytesWriter`

```cpp
class BytesWriter final : public IWriter {
public:
    explicit BytesWriter(std::vector<std::byte> &bytes);
};
```

Wraps a `std::vector<std::byte>` object as an `IWriter` interface.

### Method `write`

```cpp
task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;
```

Writes data into the byte array.

### Method `data`

```cpp
auto &&data(this Self &&self) {
    return self.mBytes;
}
```

Accesses the underlying `std::vector<std::byte>` member.

### Method `operator*`

```cpp
auto &&operator*(this Self &&self) {
    return self.mBytes;
}
```

Accesses the underlying `std::vector<std::byte>` member.

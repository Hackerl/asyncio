# Error Handling

Most of the `asyncio` APIs return [`std::expected`](https://en.cppreference.com/w/cpp/utility/expected), which is similar to Rust's `Result`.

## Error Codes

Since `C++11`, the standard error type [`std::error_code`](https://en.cppreference.com/w/cpp/error/error_code) has existed, but it has not been widely used. It started to gain attention with the popularity of `filesystem`, but still, not many developers are familiar with its principles and usage.

For most `C++` business developers, they might only want to return `false` when an error occurs and log an error message. However, this is obviously not a good practice, especially for library development. When we use a library, we need more than just a success or failure indicator—we need error codes, error messages, and sometimes, the ability to compare errors.

Thus, each library has its own set of `errno` and `strerror`, just like `Windows` has `GetLastError` and `FormatMessage`. But can we unify them all?

Certainly, this is where `std::error_code` comes in. `std::error_code` is an abstraction combining `errno` and `strerror`. It holds an error code for a particular type of error and can convert it to the corresponding error message.

The standard library already defines `POSIX` error types with [`std::generic_category`](https://en.cppreference.com/w/cpp/error/generic_category) and operating system-specific error types with [`std::system_category`](https://en.cppreference.com/w/cpp/error/system_category).

```c++
// POSIX
const std::error_code ec{EAGAIN, std::generic_category()};
assert(ec.value() == EAGAIN);
fmt::print("{:s}", ec);

// Linux
const std::error_code ec{EOPNOTSUPP, std::system_category()};
assert(ec.value() == EOPNOTSUPP);
fmt::print("{:s}", ec);

// Windows
const std::error_code ec{ERROR_FILE_NOT_FOUND, std::system_category()};
assert(ec.value() == ERROR_FILE_NOT_FOUND);
fmt::print("{:s}", ec);
```

When writing cross-platform code, how should we choose error categories?

- For `malloc` failures, we should return `std::error_code{errno, std::generic_category()}`.
- For `open` failures on `UNIX`, it is recommended to return `std::error_code{errno, std::system_category()}`.
- For `OpenFile` failures on `Windows`, we should return `std::error_code{static_cast<int>(GetLastError()), std::system_category()}`.

Interested readers can explore further materials on this topic; we will not elaborate here.

## Custom Error Codes

The error categories provided by the standard library are far from sufficient. Many of `asyncio`'s APIs need to return custom errors, such as task timeouts, task cancellations, and so on.

> If we lazily use `ETIMEDOUT` and `ECANCELED` to return errors, the source of the error will be unclear when received by the higher layers, because `std::error_code` lacks the ability to record the call stack.

However, if we define custom error categories and error messages, we can alleviate this issue somewhat.

```c++
fmt::print(stderr, "Error: {} ({})\n", ec.message(), ec);
```

When the upper layers print the error code, the terminal might display something like:

> error: task has been cancelled (asyncio::task:1)

From this, we can determine that the error category is `asyncio::task`, the error code is `1`, and the error message is `task has been cancelled`. We should have a good idea of the root cause.

Unfortunately, defining custom error categories and codes is quite tedious. We need to define our own error code enumeration and also inherit from [`std::error_category`](https://en.cppreference.com/w/cpp/error/error_category) while overriding some virtual functions.

At the early stages of the project, I attempted to define them by hand, but it became difficult to maintain. Eventually, I resorted to macro magic.

```c++
// https://github.com/Hackerl/zero/blob/master/include/zero/error.h

// .h
namespace asyncio::task {
    Z_DEFINE_ERROR_CODE_EX(
        Error,
        "asyncio::task",
        CANCELLED, "Task has been cancelled", std::errc::operation_canceled,
        CANCELLATION_NOT_SUPPORTED, "Task does not support cancellation", std::errc::operation_not_supported,
        LOCKED, "Task is locked", std::errc::resource_unavailable_try_again,
        WILL_BE_DONE, "Operation will be done soon", Z_DEFAULT_ERROR_CONDITION
    )
}

Z_DECLARE_ERROR_CODE(asyncio::task::Error)

// .cpp
Z_DEFINE_ERROR_CATEGORY_INSTANCE(asyncio::task::Error)
```

The above code is part of `asyncio`, where we define a new error category, error codes, and corresponding messages using three macros.

When a task is successfully cancelled, we can simply return `std::unexpected{task::Error::CANCELLED}`.

## Error Conditions

In C++'s error system, `std::error_code` represents the specific error that occurred. We can certainly compare two error codes for equality to determine the exact cause of an error. However, what if we just want to know if it's a timeout error or a cancellation-related error? We certainly don't want to compare it with all possible error codes.

If you've been paying attention, you might have noticed that in the previous section on custom error codes, we used `std::errc`:

```c++
Z_DEFINE_ERROR_CODE_EX(
    Error,
    "asyncio::task",
    CANCELLED, "Task has been cancelled", std::errc::operation_canceled,
    CANCELLATION_NOT_SUPPORTED, "Task does not support cancellation", std::errc::operation_not_supported,
    LOCKED, "Task is locked", std::errc::resource_unavailable_try_again,
    WILL_BE_DONE, "Operation will be done soon", Z_DEFAULT_ERROR_CONDITION
)
```

`std::error_condition` represents a general error condition, and `std::errc` provides standard library-defined `error_condition` values corresponding to `POSIX` errors.

When defining custom errors, we can specify their associated error conditions, so comparing the error code with the error condition will yield equality:

```c++
assert(std::error_code{asyncio::task::CANCELLED} == std::errc::operation_canceled);
```

We can also define custom error conditions:

```c++
namespace asyncio {
    Z_DEFINE_ERROR_CONDITION(
        IOError,
        "asyncio::io",
        UNEXPECTED_EOF, "Unexpected end of file"
    )
}
```

In `asyncio`, I defined an `IOError` error condition, where `UNEXPECTED_EOF` indicates encountering an unexpected EOF.

The specific error code is defined when implementing the interface:

```c++
namespace asyncio {
    class IReader : public virtual zero::Interface {
    public:
        Z_DEFINE_ERROR_CODE_INNER_EX(
            ReadExactlyError,
            "asyncio::IReader",
            UNEXPECTED_EOF, "Unexpected end of file", make_error_condition(IOError::UNEXPECTED_EOF)
        )

        virtual task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) = 0;
        virtual task::Task<void, std::error_code> readExactly(std::span<std::byte> data);
        virtual task::Task<std::vector<std::byte>, std::error_code> readAll();
    };
}
```

The `readExactly` method expects to read an exact number of bytes. If it encounters an EOF while reading only part of the data, it will return the `asyncio::ReadExactlyError::UNEXPECTED_EOF` error.

The caller may not care about the specific error codes but may only care if the error belongs to a particular category:

```c++
assert(std::error_code{asyncio::ReadExactlyError::UNEXPECTED_EOF} == asyncio::IOError::UNEXPECTED_EOF);
```

## Error Transformation

As mentioned in the first section, third-party libraries use a variety of error handling methods, but they all generally rely on variants of `errno` and `strerror`. We can manually convert these into `error_code`, but it would be very time-consuming, so I prefer to handle this with macros:

```c++
namespace asyncio::net::tls {
    Z_DEFINE_ERROR_TRANSFORMER(
        OpenSSLError,
        "asyncio::net::tls::openssl",
        ([](const int value) -> std::string {
            std::array<char, 1024> buffer{};
            ERR_error_string_n(static_cast<unsigned long>(value), buffer.data(), buffer.size());
            return buffer.data();
        })
    )
}
```

`OpenSSL`'s error handling is similar to `errno` and `strerror`, so we simply define a `transformer` to convert `OpenSSL` errors into `error_code`:

```c++
const std::error_code ec{static_cast<OpenSSLError>(ERR_get_error())};

if (ec == static_cast<OpenSSLError>(OPENSSL_ERROR_CONSTANT))
    xxxx;

fmt::print("{:s}", ec);
```

In the above `transformer`, we specify how the error code should be converted to an error description, but we haven't defined the mapping to error conditions. Therefore, we can only compare it with specific error codes. The main reason is that `OpenSSL`'s error system is complex, with many types, making it difficult to handle.

If you have the patience, you can explicitly list the mappings as follows:

```c++
namespace asyncio::uv {
    Z_DEFINE_ERROR_TRANSFORMER_EX(
        Error,
        "asyncio::uv",
        ([](const int value) -> std::string {
            std::array<char, 1024> buffer{};
            uv_strerror_r(value, buffer.data(), buffer.size());
            return buffer.data();
        }),
        [](const int value) -> std::optional<std::error_condition> {
            switch (value) {
            case UV_E2BIG:
                return std::errc::argument_list_too_long;

            case UV_EACCES:
                return std::errc::permission_denied;

            // ...

            default:
                return std::nullopt;
            }
        }
    )
}
```

`asyncio` is based on `libuv`, and most of `libuv`'s errors come from system errors. I used the power of `ChatGPT` to map the relationships one by one.

## Error Tracing

Error tracing is a painful task. `error_code` cannot carry any dynamic data for tracing, and its error descriptions are typically mapped constants. It also doesn't have any reserved fields for extension, making it difficult to expand. I believe the `C++` committee won't change this situation either.

After many failed attempts, I decided to temporarily put this issue aside. If the day comes when I really need to address it, I will replace `std::error_code` with a custom `error_code` type:

```c++
class ErrorCode : public std::error_code {
private:
#ifndef NDEBUG
    std::stacktrace mStacktrace;
#endif
};
```

> This isn't particularly difficult to implement; I just haven't found a compelling reason to do it yet.

## Error Propagation

For every potentially failing interface, we should check the result of the call. Most of the time, when we detect an error, we will choose to propagate it up immediately:

```c++
const auto result = func();

if (!result)
    return std::unexpected{result.error()};
```

However, I don't want to write this long code every time an error check is made. That would make the code look like Go's error handling. Unfortunately, C++ does not have Rust's question mark syntax sugar. So, what should we do?

Perhaps we can create our own solution:

```c++
// https://github.com/Hackerl/zero/blob/master/include/zero/expect.h

#ifdef __GNUC__
#define TRY(...)                                                    \
    ({                                                              \
        auto &&_result = __VA_ARGS__;                               \
                                                                    \
        if (!_result)                                               \
            return std::unexpected{std::move(_result).error()};     \
                                                                    \
        *std::move(_result);                                        \
    })
#endif

#ifdef __clang__
#define CO_TRY(...)                                                 \
    ({                                                              \
        auto &&_result = __VA_ARGS__;                               \
                                                                    \
        if (!_result)                                               \
            co_return std::unexpected{std::move(_result).error()};  \
                                                                    \
        *std::move(_result);                                        \
    })
#endif
```

By using `statement expressions`, we can simulate Rust's question mark syntax sugar:

```c++
const auto result1 = TRY(func());
const auto result2 = CO_TRY(co_await func1());
```

Unfortunately, `MSVC` does not support this extended syntax, and GCC's support is not ideal either. So, I had to settle for an alternative:

```c++
#define Z_EXPECT(...)                                                 \
    if (auto &&_result = __VA_ARGS__; !_result)                     \
        return std::unexpected{std::move(_result).error()}

#define Z_CO_EXPECT(...)                                              \
    if (auto &&_result = __VA_ARGS__; !_result)                     \
        co_return std::unexpected{std::move(_result).error()}
```

For every `std::expected` variable, we need to perform a post-check:

```c++
const auto result1 = func();
EXPECT(result1);

const auto result2 = co_await func1();
Z_CO_EXPECT(result2);
```

> I have kept the `TRY` implementation as well. If your project is guaranteed to only use the `Clang` compiler, using them is also a viable option.

# Error Handling

Most of the `asyncio` APIs return [`std::expected`](https://en.cppreference.com/w/cpp/utility/expected), which is similar to Rust's `Result`. However, this doesn't mean I'm against exceptions. On the contrary, I extensively use exceptions in upper-layer business code. Error codes and exceptions are not incompatible — combining them perfectly creates an excellent error handling system.

## Error Codes

Since `C++11`, the standard error type [`std::error_code`](https://en.cppreference.com/w/cpp/error/error_code) has existed, but not many people have used it. It finally got a chance to shine with the popularity of `filesystem`, but still, very few people think about understanding its principles and usage in depth.

For most `C++` business developers, they might only want to return `false` when an error occurs and leave an error log. However, this is obviously not a good practice, especially for library development. When we use a library, we need more than just knowing whether the call succeeded — we need error codes, error messages, and even the ability to compare errors.

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

## When to Use Error Codes?

Suppose you are writing a series of low-level, general-purpose functions to expose as library interfaces for others to call, such as `readFile` or `httpGet`. You might find they have some common characteristics:

- They have a high likelihood of returning errors, such as file not found or connection failure.
- These returned errors sometimes need to be handled manually, such as executing additional logic when a file doesn't exist, rather than always propagating errors upward.
- Callers may want to determine from the function signature whether the API might return errors.
- They might be called in multiple places in business code, and if their error codes are thrown directly to the top layer, the source of the error will be difficult to trace.

So when writing such APIs, I choose `std::expected<T, std::error_code>` as the return value. However, as mentioned above, if low-level error codes are passed directly in business code, the source of errors will be difficult to analyze — "No such file or directory" from which call?

## Exceptions

Exceptions are always criticized for their terrible performance or their unexpected appearances. So I would never use exceptions as the error handling method for general-purpose APIs, but that doesn't mean exceptions won't be thrown inside APIs.

```c++
std::expected<asyncio::http::URL, std::error_code> asyncio::http::URL::from(const std::string &str) {
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url{curl_url(), curl_url_cleanup};

    if (!url)
        throw zero::error::StacktraceError<std::system_error>{errno, std::generic_category()};

    Z_EXPECT(expected([&] {
        return curl_url_set(url.get(), CURLUPART_URL, str.c_str(), CURLU_NON_SUPPORT_SCHEME);
    }));

    return URL{std::move(url)};
}
```

Errors that cannot be handled (such as out of memory) will be thrown as exceptions. This also indirectly reflects my attitude toward exceptions — when an exception occurs, it means the program has encountered an unrecoverable error and can only be forced to exit.

Therefore, I extensively use exceptions in upper-layer business code. This code also has some common characteristics:

- It combines multiple low-level APIs to complete a series of logical operations, sometimes manually handling errors returned by APIs, but most of the time directly throwing errors upward.
- When calling other business code, it assumes exceptions will be thrown and usually doesn't catch them, because the failure of one operation causing the entire call chain to be interrupted is expected behavior.

When an exception is thrown to the top layer, we certainly hope the exception message can help us trace back to the error source. The `stacktrace` module added in `C++23` can help us obtain the call stack, so I encapsulated a generic exception type `StacktraceError`:

```c++
namespace zero::error {
#if defined(__cpp_lib_stacktrace) && __cpp_lib_stacktrace >= 202011L
    template<typename T>
        requires std::derived_from<T, std::exception>
    class StacktraceError final : public T {
    public:
        template<typename... Args>
        explicit StacktraceError(Args &&... args)
            : T{std::forward<Args>(args)...},
              mMessage{fmt::format("{} {}", T::what(), std::to_string(std::stacktrace::current(1)))} {
        }

        [[nodiscard]] const char *what() const noexcept override {
            return mMessage.c_str();
        }

    private:
        std::string mMessage;
    };
#else
    template<typename T>
    using StacktraceError = T;
#endif
}
```

However, `stacktrace` obtains the synchronous call stack, not the coroutine one, and it cannot work when debug information is removed. Fortunately, in `asyncio` we can easily complete call stack tracing:

```c++
fmt::print("{}\n", fmt::join(co_await asyncio::task::backtrace, "\n"));
```

> Even without debug information, `asyncio::task::backtrace` can still work normally.

So I also encapsulated a `StacktraceError` for `asyncio`:

```c++
namespace asyncio::error {
    template<typename T>
        requires std::derived_from<T, std::exception>
    class StacktraceError final : public T {
    public:
        template<typename... Args>
        explicit StacktraceError(const std::vector<std::source_location> &stacktrace, Args &&... args)
            : T{std::forward<Args>(args)...},
              mMessage{fmt::format("{} {}", T::what(), to_string(fmt::join(stacktrace | std::views::drop(1), "\n")))} {
        }

        template<typename... Args>
        static task::Task<StacktraceError> make(Args &&... args) {
            co_return StacktraceError{co_await task::backtrace, std::forward<Args>(args)...};
        }

        [[nodiscard]] const char *what() const noexcept override {
            return mMessage.c_str();
        }

    private:
        std::string mMessage;
    };
}
```

It obtains the current coroutine's call stack and appends it to the exception message before throwing.

## Error Code to Exception

In business code, when an API call fails, we usually want to throw the error directly upward:

```c++
const std::expected<std::string, std::error_code> content = readFile(path);

if (!content)
    throw std::system_error{content.error()};
```

`std::error_code` can be directly converted to a `std::system_error` exception, which looks convenient, right? However, problems arise. After catching the exception at the top layer and printing a message — "No such file or directory" — you start to wonder, where exactly did this come from? You're not even sure which API call triggered it. After calming down, you carefully analyze and guess it's probably caused by filesystem operations, so you start searching the codebase for file operation calls. The search results make you dizzy — there are dozens of `readFile` calls. Which one is it?

To better trace errors, we switch to using `StacktraceError`:

```c++
const std::expected<std::string, std::error_code> content = readFile(path);

if (!content)
    throw zero::error::StacktraceError<std::system_error>{content.error()};
```

For coroutines, we of course want to see the asynchronous call stack:

```c++
const std::expected<std::string, std::error_code> content = co_await readFile(path);

if (!content)
    throw co_await asyncio::error::StacktraceError<std::system_error>::make(content.error());
```

We frequently check for errors and convert them to exceptions. Can't we do this more elegantly? So I wrote some helper functions:

```c++
namespace zero::error {
    template<typename T, typename E>
        requires std::is_convertible_v<E, std::error_code>
    T guard(std::expected<T, E> &&expected) {
        if (!expected)
            throw StacktraceError<std::system_error>{expected.error()};

        if constexpr (std::is_void_v<T>)
            return;
        else
            return *std::move(expected);
    }
}

namespace asyncio::error {
    template<typename T, typename E>
        requires std::is_convertible_v<E, std::error_code>
    task::Task<T> guard(std::expected<T, E> expected) {
        if (!expected)
            throw co_await StacktraceError<std::system_error>::make(expected.error());

        if constexpr (std::is_void_v<T>)
            co_return;
        else
            co_return *std::move(expected);
    }

    template<typename T, typename E>
        requires std::is_convertible_v<E, std::error_code>
    task::Task<T> guard(task::Task<T, E> task) {
        auto result = co_await task;

        if (!result)
            throw co_await StacktraceError<std::system_error>::make(result.error());

        if constexpr (std::is_void_v<T>)
            co_return;
        else
            co_return *std::move(result);
    }
}
```

Using them is also very convenient:

```c++
const std::string content = zero::error::guard(readFile(path));
const std::string content = co_await asyncio::error::guard(readFile(path));
```

## Custom Error Codes

The error categories provided by the standard library are far from sufficient. Many of `asyncio`'s APIs need to return custom errors, such as task locked, task cannot be cancelled, and so on.

Unfortunately, defining custom error categories and codes is quite tedious. We need to define our own error code enumeration and also inherit from [`std::error_category`](https://en.cppreference.com/w/cpp/error/error_category) while overriding some virtual functions.

At the early stages of the project, I attempted to define them by hand, but it became difficult to maintain. Eventually, I resorted to macro magic.

```c++
// https://github.com/Hackerl/zero/blob/master/include/zero/error.h

// .h
namespace asyncio::task {
    Z_DEFINE_ERROR_CODE_EX(
        Error,
        "asyncio::task",
        Cancelled, "Task was cancelled", std::errc::operation_canceled,
        CancellationNotSupported, "Task does not support cancellation", std::errc::operation_not_supported,
        Locked, "Task is locked", std::errc::resource_unavailable_try_again,
        CancellationTooLate, "Cancellation is too late", std::errc::operation_not_permitted,
        AlreadyCompleted, "Task is already completed", std::errc::operation_not_permitted
    )
}

Z_DECLARE_ERROR_CODE(asyncio::task::Error)

// .cpp
Z_DEFINE_ERROR_CATEGORY_INSTANCE(asyncio::task::Error)
```

The above is real code from `asyncio`, where we easily define a new error category, error codes, and corresponding messages using three macros.

When a task is successfully cancelled, we can simply return `std::unexpected{task::Error::Cancelled}`.

## Error Conditions

In C++'s error system, `std::error_code` represents the specific error that occurred. We can certainly compare two error codes for equality to determine the exact cause of an error. However, what if we just want to know if it's a timeout error or if it's caused by a file not existing? We certainly don't want to compare it with all possible error codes. If you've been paying attention, you might have noticed that in the previous section on custom error codes, we used `std::errc`:

```c++
Z_DEFINE_ERROR_CODE_EX(
    Error,
    "asyncio::task",
    Cancelled, "Task was cancelled", std::errc::operation_canceled,
    CancellationNotSupported, "Task does not support cancellation", std::errc::operation_not_supported,
    Locked, "Task is locked", std::errc::resource_unavailable_try_again,
    CancellationTooLate, "Cancellation is too late", std::errc::operation_not_permitted,
    AlreadyCompleted, "Task is already completed", std::errc::operation_not_permitted
)
```

`std::error_condition` represents a general error condition, and `std::errc` provides standard library-defined `error_condition` values corresponding to `POSIX` errors. When defining custom errors, we can specify their associated error conditions, so comparing the error code with the error condition will yield equality:

```c++
assert(std::error_code{asyncio::task::Cancelled} == std::errc::operation_canceled);
```

If we want to execute an additional `fallback` strategy when a file doesn't exist, we can write:

```c++
asyncio::task::Task<void> func() {
    // ...
    const auto content = co_await readFile(path);

    if (!content) {
        if (const auto &error = content.error(); error != std::errc::no_such_file_or_directory)
            throw co_await asyncio::error::StacktraceError<std::system_error>::make(error);

        // ...
    }

    // ...
}
```

> We don't care whether the error is `ENOENT` or `ERROR_FILE_NOT_FOUND`; we only care if it means the file doesn't exist.

Of course, we can also define custom error conditions:

```c++
namespace asyncio {
    Z_DEFINE_ERROR_CONDITION(
        IOError,
        "asyncio::io",
        UnexpectedEOF, "Unexpected end of file"
    )
}
```

In `asyncio`, I defined an `IOError` error condition, where `UnexpectedEOF` indicates encountering an unexpected EOF.

The specific error code is defined when implementing the interface:

```c++
namespace asyncio {
    class IReader : public virtual zero::Interface {
    public:
        Z_DEFINE_ERROR_CODE_INNER_EX(
            ReadExactlyError,
            "asyncio::IReader",
            UnexpectedEOF, "Unexpected end of file", make_error_condition(IOError::UnexpectedEOF)
        )

        virtual task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) = 0;
        virtual task::Task<void, std::error_code> readExactly(std::span<std::byte> data);
        virtual task::Task<std::vector<std::byte>, std::error_code> readAll();
    };
}
```

The `readExactly` method expects to read an exact number of bytes. If it encounters an EOF while reading only part of the data, it will return the `asyncio::ReadExactlyError::UnexpectedEOF` error.

Upper-layer callers may not care about or remember all the specific error codes; they only care whether the error falls into a certain category:

```c++
assert(std::error_code{asyncio::ReadExactlyError::UnexpectedEOF} == asyncio::IOError::UnexpectedEOF);
```

## Error Transformation

As mentioned in the first section, third-party libraries use a variety of error handling methods, but they all generally rely on variants of `errno` and `strerror`. We can manually convert these into error codes, but it would be very time-consuming, so I prefer to handle this with macros:

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

`OpenSSL`'s error handling is similar to `errno` and `strerror`, so we simply define a `transformer` to convert `OpenSSL` errors into error codes:

```c++
const std::error_code ec{static_cast<OpenSSLError>(ERR_get_error())};

if (ec == static_cast<OpenSSLError>(OPENSSL_ERROR_CONSTANT))
    xxxx;

fmt::print("{:s}", ec);
```

In the above `transformer`, we specify how the error code should be converted to an error description, but we haven't defined the mapping to error conditions. Therefore, we can only compare it with specific error codes. The main reason is that `OpenSSL`'s error system is complex, with many types, making it difficult to handle.

Of course, if you have the patience and perseverance, you can explicitly list the mappings as follows:

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

## Error Propagation

When writing general-purpose APIs, we should check the return value for every potentially failing call. Most of the time, when we detect an error, we will choose to throw it directly to the upper layer:

```c++
std::expected<void, std::error_code> api() {
    const auto result = func();

    if (!result)
        return std::unexpected{result.error()};
}
```

However, I don't want to write this long code every time an error check is made. That would make it look like Go's style. Unfortunately, C++ does not have Rust's question mark syntax sugar. So, what should we do?

Perhaps we can create our own:

```c++
// https://github.com/Hackerl/zero/blob/master/include/zero/expect.h

#ifdef __GNUC__
#define Z_TRY(...)                                                  \
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
#define Z_CO_TRY(...)                                               \
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

We can simulate Rust's question mark syntax sugar using `statement expressions`:

```c++
const auto result1 = Z_TRY(func());
const auto result2 = Z_CO_TRY(co_await func1());
```

Unfortunately, `MSVC` does not support this extended syntax, and GCC's support is also subpar. So I had to settle for an alternative:

```c++
#define Z_EXPECT(...)                                               \
    if (auto &&_result = __VA_ARGS__; !_result)                     \
        return std::unexpected{std::move(_result).error()}

#define Z_CO_EXPECT(...)                                            \
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

> I still kept the `TRY` implementations. If your project is certain to only use `Clang` for compilation, using them is also viable.

## Best Practices
### Business Code

In business code, use only exceptions for error propagation.

- For functions returning `std::expected<T, std::error_code>`, use `guard` for error checking, which automatically throws on error.

In synchronous functions, use `zero::error::guard`:

```c++
void func() {
    // ...
    const auto content = zero::error::guard(readFile(path));
    // ...
}
```

In coroutines, use `asyncio::error::guard`:

```c++
asyncio::task::Task<void> func() {
    // ...
    const auto content = co_await asyncio::error::guard(readFile(path));
    // ...
}
```

- Convert generic system API return values to `std::expected`, then handle with `guard`.

On `Windows`, APIs that return `BOOL` are generic system APIs — returning `TRUE` indicates success, returning `FALSE` indicates failure, and `GetLastError` is used to get the error code on failure.

```c++
void func() {
    // ...
    zero::error::guard(zero::os::windows::expected([&] {
        return CloseHandle(handle);
    }));
    // ...
}

asyncio::task::Task<void> func() {
    // ...
    co_await asyncio::error::guard(zero::os::windows::expected([&] {
        return CloseHandle(handle);
    }));
    // ...
}
```

On `UNIX`, APIs that return integers or pointers are generic system APIs — returning 0 indicates success, returning -1 indicates failure, and `errno` is used to get the error code on failure.

```c++
void func() {
    // ...
    zero::error::guard(zero::os::unix::expected([&] {
        return close(fd);
    }));
    // ...
}

asyncio::task::Task<void> func() {
    // ...
    co_await asyncio::error::guard(zero::os::unix::expected([&] {
        return close(fd);
    }));
    // ...
}
```

- For special system APIs, manually check for errors and convert to exceptions.

```c++
void func() {
    // ...
    const auto handle = CreateFileA(
        R"(\\.\NUL)",
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE)
        throw zero::error::StacktraceError<std::system_error>{static_cast<int>(GetLastError()), std::system_category()};
    // ...
}

asyncio::task::Task<void> func() {
    // ...
    const auto handle = CreateFileA(
        R"(\\.\NUL)",
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE)
        throw co_await asyncio::error::StacktraceError<std::system_error>::make(static_cast<int>(GetLastError()), std::system_category());
    // ...
}
```

> APIs that return `HANDLE` sometimes return `NULL` on failure and sometimes return `INVALID_HANDLE_VALUE`, which is why `zero::os::windows::expected` doesn't support such APIs.

- Throw exceptions directly for internal errors.

```c++
void func() {
    // ...
    if (name.empty())
        throw zero::error::StacktraceError<std::runtime_error>{"Empty name"};
    // ...
}

asyncio::task::Task<void> func() {
    // ...
    if (name.empty())
        throw co_await asyncio::error::StacktraceError<std::runtime_error>::make("Empty name");
    // ...
}
```

### General-Purpose API Code

If you need to write general-purpose functions to expose as library interfaces for others to call, use `std::expected` as the return value.

- For functions returning `std::expected<T, std::error_code>`, use the `EXPECT` macro for error checking, which automatically throws on error.

```c++
std::expected<void, std::error_code> func() {
    // ...
    const auto content = readFile(path);
    Z_EXPECT(content);
    // ...
}

asyncio::task::Task<void, std::error_code> func() {
    // ...
    const auto content = co_await readFile(path);
    Z_CO_EXPECT(content);
    // ...
}
```

- Convert generic system API return values to `std::expected`, then handle with `EXPECT`.

```c++
std::expected<void, std::error_code> func() {
    // ...
    Z_EXPECT(zero::os::windows::expected([&] {
        return CloseHandle(handle);
    }));
    // ...
}

asyncio::task::Task<void, std::error_code> func() {
    // ...
    Z_CO_EXPECT(co_await zero::os::windows::expected([&] {
        return CloseHandle(handle);
    }));
    // ...
}
```

- For special system APIs, manually check for errors and convert to `std::error_code`.

```c++
std::expected<void, std::error_code> func() {
    // ...
    const auto handle = CreateFileA(
        R"(\\.\NUL)",
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE)
        return std::unexpected{static_cast<int>(GetLastError()), std::system_category()};
    // ...
}

asyncio::task::Task<void, std::error_code> func() {
    // ...
    const auto handle = CreateFileA(
        R"(\\.\NUL)",
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE)
        co_return std::unexpected{static_cast<int>(GetLastError()), std::system_category()};
    // ...
}
```

- Return custom error codes for internal errors.

```c++
Z_DEFINE_ERROR_CODE(
    Error,
    "Error",
    EmptyName, "Empty name"
)

Z_DECLARE_ERROR_CODE(Error)

Z_DEFINE_ERROR_CATEGORY_INSTANCE(Error)

std::expected<void, std::error_code> func() {
    // ...
    if (name.empty())
        return std::unexpected{Error::EmptyName};
    // ...
}

asyncio::task::Task<void, std::error_code> func() {
    // ...
    if (name.empty())
        co_return std::unexpected{Error::EmptyName};
    // ...
}
```

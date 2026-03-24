# 错误处理

`asyncio` 的绝大多数 `API` 都返回 [`std::expected`](https://en.cppreference.com/w/cpp/utility/expected)，它类似于 `Rust` 的 `Result`。但这并不意味着我反对异常，相反，在上层的业务代码中我大量使用了异常。错误码和异常并非水火不容，将它们完美结合才能造就一套优秀的错误处理体系。

## 错误码

从 `C++11` 开始就有了标准错误类型 [`std::error_code`](https://en.cppreference.com/w/cpp/error/error_code)，只不过使用过的人并不多，随着 `filesystem` 的普及它才终于有了机会展露头角，但却还是很少有人会想深入了解它的原理和用法。

对于大多数的 `C++` 业务开发者来说，他们或许只想在发生错误时返回 `false`，并留下一行错误日志。但这显然不是一个好的做法，特别是对于库的开发来说。
当我们在使用某个库时，想知道的绝不仅仅是调用是否成功，我们需要错误号，需要错误信息，甚至需要比较错误。

所以每个库都会有一套自己的 `errno` 和 `strerror`，就像 `Windows` 也有自己的 `GetLastError` 和 `FormatMessage`，那可以将它们全都统一起来吗？

当然可以，这就是 `std::error_code` 出现的原因。`std::error_code` 就是抽象之后 `errno` 与 `strerrno` 的结合，它持有某种错误种类的错误号，又能够将将之转换为对应的字符串信息。

标准库已经帮我们定义好了 `POSIX` 错误种类 [`std::generic_category`](https://en.cppreference.com/w/cpp/error/generic_category)，以及操作系统错误种类 [`std::system_category`](https://en.cppreference.com/w/cpp/error/system_category)。

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

在编写跨平台的代码时，我们应该如何选择错误种类呢：

- 对于 `malloc` 失败，我们应该返回 `std::error_code{errno, std::generic_category()}`。
- 对于 `UNIX` 上的 `open` 失败，建议返回 `std::error_code{errno, std::system_category()}`。
- 对于 `Windows` 上的 `OpenFile` 失败，应该返回 `std::error_code{static_cast<int>(GetLastError()), std::system_category()}`。

关于这部分，感兴趣的可以自行查阅相关资料，此处不再赘述。

## 什么时候使用错误码？

假设你正在编写一系列底层的通用函数，作为库接口暴露给他人调用，例如 `readFile`、`httpGet`，你可能会发现它们有一些共通的特点：

- 它们有很大的可能会返回错误，例如文件不存在、连接失败等等。
- 这些返回的错误，有时候希望被手动处理，例如文件不存在则执行额外的逻辑，而不是一味地向上传播错误。
- 调用者可能希望从函数签名就能判断出该 `API` 是否可能返回错误。
- 它们可能在业务代码的多个地方被调用，如果它们的错误码直接上抛到顶层时，错误来源将难以追溯。

所以在编写这类 `API` 时，我选择 `std::expected<T, std::error_code>` 作为返回值。但就像上面说的，底层的错误码如果直接在业务代码层面传递，错误的源头将难以分析 —— “No such file or directory” 到底是哪一次调用产生的？

## 异常
异常总是为人所诟病，它那糟糕的性能，亦或是它出其不意的闪现。所以我绝不会将异常作为通用 `API` 的错误处理方式，但这并不意味着 `API` 内部不会抛出异常。

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

那些无法被处理的错误（例如内存不足），将会当作异常抛出。这也间接反映出了我对待异常的态度 —— 当异常产生时，说明程序遇到了无法恢复的错误，只能被迫退出。

因此我在上层业务代码中大量使用了异常，这些代码也有一些共通的特点：

- 它们组合调用多个底层 `API` 完成一系列逻辑操作，有时会手动处理 `API` 返回的错误，大多数时候则会直接将错误上抛。
- 它们调用其它的业务代码时，默认它会抛出异常，大多数时候并不会捕获它，因为一个操作的失败导致整个调用链中断是预期内的行为。

当一个异常抛出到顶层时，我们一定希望异常消息能够帮助我们追溯到错误源头。而 `C++23` 新增的 `stacktrace` 模块正好可以帮助我们获取调用栈，所以我封装了一个通用的异常类型 `StacktraceError`：

```c++
namespace zero::error {
#if defined(__cpp_lib_stacktrace) && __cpp_lib_stacktrace >= 202011L
    template<std::derived_from<std::exception> T>
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

但是 `stacktrace` 获取的是同步的调用栈，而非协程的，并且它无法在去除了调试信息的情况下工作。幸运的是，在 `asyncio` 中我们可以轻松地完成调用栈回溯：

```c++
fmt::print("{}\n", fmt::join(co_await asyncio::task::backtrace, "\n"));
```

> 即使没有调试信息，`asyncio::task::backtrace` 也可以正常工作。

所以我又为 `asyncio` 封装了一个 `StacktraceError`：

```c++
namespace asyncio::error {
    template<std::derived_from<std::exception> T>
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

它会获取当前协程的调用栈，并附加到异常信息中抛出。

## 错误码转异常

在业务代码中，调用某个 `API` 失败时，我们通常希望将错误直接上抛：

```c++
const std::expected<std::string, std::error_code> content = readFile(path);

if (!content)
    throw std::system_error{content.error()};
```

`std::error_code` 可以直接转为 `std::system_error` 异常，看起来很方便不是吗？然而问题随之而来，顶层捕获到异常后打印出一条消息 —— “No such file or directory”；你开始困惑，这到底是哪里抛出来的？你甚至都不确定这是调用哪个 `API` 触发的。冷静下来后，你仔细分析，猜测大概率是文件系统的操作引起的，于是你开始在代码库中搜索文件操作相关的调用；搜索结果却让你两眼一黑，足足有几十处调用了 `readFile`，到底是哪一处呢？

为了更好地错误溯源，于是我们转而使用 `StacktraceError`：

```c++
const std::expected<std::string, std::error_code> content = readFile(path);

if (!content)
    throw zero::error::StacktraceError<std::system_error>{content.error()};
```

对于协程我们当然希望看到的是异步的调用栈：

```c++
const std::expected<std::string, std::error_code> content = co_await readFile(path);

if (!content)
    throw co_await asyncio::error::StacktraceError<std::system_error>::make(content.error());
```

我们频繁地判断错误并转为异常，为什么不能更优雅一些呢？所以我编写了一些辅助函数：

```c++
namespace zero::error {
    template<typename T, std::convertible_to<std::error_code> E>
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
    template<typename T, std::convertible_to<std::error_code> E>
    task::Task<T> guard(std::expected<T, E> expected) {
        if (!expected)
            throw co_await StacktraceError<std::system_error>::make(expected.error());

        if constexpr (std::is_void_v<T>)
            co_return;
        else
            co_return *std::move(expected);
    }

    template<typename T, std::convertible_to<std::error_code> E>
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

使用起来也很便利：

```c++
const std::string content = zero::error::guard(readFile(path));
const std::string content = co_await asyncio::error::guard(readFile(path));
```

## 自定义错误码

标准库提供的错误种类当然远远不够，`asyncio` 中的众多 `API` 都需要返回自定义的错误，例如任务被锁定、任务无法取消等等。

不幸的是，自定义错误种类、错误码是一件极其繁琐的事情，我们需要定义自己的错误号枚举类型，还需要继承 [`std::error_category`](https://en.cppreference.com/w/cpp/error/error_category) 并重载部分虚函数。

项目前期，我曾尝试过一个个手写，但却难以坚持，不得已之下又只能求助于宏魔法。

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

上面就是 `asyncio` 中真实存在的代码，使用三个宏我们轻松定义了一个新的错误种类，还有错误码及对应的错误信息。

当某个任务被成功取消时，我们只需要 `return std::unexpected{task::Error::Cancelled}`。

## 错误条件

在 `C++` 的错误体系中，`std::error_code` 代表具体产生的错误，我们当然可以比较两个 `error code` 是否相等来判断具体的错误原因。
但是如果我们只是想知道它是不是一个超时错误，或是文件不存在导致的，那应该怎么办呢？我们总不可能将它与所有可能出现的 `error code` 都比较一遍吧。
细心的你可能已经注意到了，在上一节自定义错误码的代码中，使用到了 `std::errc`：

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

`std::error_condition` 代表一种错误条件，`std::errc` 就是标准库提供的与 `posix` 错误相对应的 `error condition`。我们在自定义错误时，可以指定错误对应的错误条件，那么将 `error code` 与对应的 `error condtion` 比较时就会相等：

```c++
assert(std::error_code{asyncio::task::Cancelled} == std::errc::operation_canceled);
```

如果我们希望在文件不存在时执行额外的 `fallback` 策略，那么可以这么写：

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

> 我们不在乎错误到底是 `ENOENT` 还是 `ERROR_FILE_NOT_FOUND`，我们只关心它的出现是否意味着文件不存在。

当然，我们也可以自定义 `error condtion`：

```c++
namespace asyncio {
    Z_DEFINE_ERROR_CONDITION(
        IOError,
        "asyncio::io",
        UnexpectedEOF, "Unexpected end of file"
    )
}
```

在 `asyncio` 中，我定义了一个 `IOError` 的错误条件，其中 `UnexpectedEOF` 用来表示遇到了预料之外的 `EOF`。

而具体的 `error code`，则在实现具体的接口时定义：

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

`readExactly` 希望读取到准确数量的数据之后返回，如果读取到部分数据便遇到了 `EOF`，它便会返回 `asyncio::ReadExactlyError::UnexpectedEOF` 错误。

上层的调用者可能不关心，也记不住那么多具体的错误码，他们只关心错误是否属于某一种情况：

```c++
assert(std::error_code{asyncio::ReadExactlyError::UnexpectedEOF} == asyncio::IOError::UnexpectedEOF);
```

## 错误转换

正如第一小节所说的，第三方库的错误处理方式五花八门、各不相同，但是万变不离其宗，它们大多都是 `errno` 与 `strerror` 的变体。
我们当然也可以手动地将之转换为 `error code`，但是要耗费极大的精力与时间，所以我依旧选择使用宏进行处理：

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

`OpenSSL` 的错误处理方式便类似于 `errno` 与 `strerror`，所以我们简单地定义了一个 `transformer`，使得 `OpenSSL` 的错误可以转为 `error code`：

```c++
const std::error_code ec{static_cast<OpenSSLError>(ERR_get_error())};

if (ec == static_cast<OpenSSLError>(OPENSSL_ERROR_CONSTANT))
    xxxx;

fmt::print("{:s}", ec);
```

当然，在上面的 `transformer` 中，我们只是表明了错误号该如何转为错误描述，我们并没有定义与错误条件的映射关系，所以只能将之与具体的错误数进行比较。
主要的原因当然是 `OpenSSL` 的错误体系较为复杂，种类繁多，不方便处理。

当然，如果你有恒心、有毅力，可以像下面一样列举出映射关系：

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

`asyncio` 基于 `libuv`，`libuv` 的错误大多都来自系统错误，所以我借助于 `ChatGPT` 的力量一一列出了映射关系。

## 错误传递

在编写通用 `API` 时，每一个可能失败的调用我们都应该检查返回值。大多数时候，当我们检查到错误后，都会选择直接抛出给上层：

```c++
std::expected<void, std::error_code> api() {
    const auto result = func();

    if (!result)
        return std::unexpected{result.error()};
}
```

但我不想在每个错误检查的地方都写上这么一长串代码，那样岂不是变成了 `Golang` 的模样？可是 `C++` 又没有 `Rust` 的问号语法糖，那该怎么办呢？

或许我们可以自己创造一个：

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

我们借助 `statement expression` 可以模拟出 `Rust` 问号语法糖：

```c++
const auto result1 = Z_TRY(func());
const auto result2 = Z_CO_TRY(co_await func1());
```

可惜，`MSVC` 并不支持该拓展语法，`GCC` 的相关支持也差强人意，于是我便只能退而求其次：

```c++
#define Z_EXPECT(...)                                               \
    if (auto &&_result = __VA_ARGS__; !_result)                     \
        return std::unexpected{std::move(_result).error()}

#define Z_CO_EXPECT(...)                                            \
    if (auto &&_result = __VA_ARGS__; !_result)                     \
        co_return std::unexpected{std::move(_result).error()}
```

每个 `std::expected` 变量我们都需要进行后置检查：

```c++
const auto result1 = func();
EXPECT(result1);

const auto result2 = co_await func1();
Z_CO_EXPECT(result2);
```

> 我依旧保留了 `TRY` 的相关实现，如果你的项目确定只会使用 `Clang` 编译，使用它们也未尝不可。

## 最佳实践
### 业务代码

业务代码中只使用异常进行错误传播。

- 返回值为 `std::expected<T, std::error_code>` 的函数使用 `guard` 进行错误检查，出错时自动上抛。

在同步函数中，使用 `zero::error::guard`：

```c++
void func() {
    // ...
    const auto content = zero::error::guard(readFile(path));
    // ...
}
```

在协程中，使用 `asyncio::error::guard`：

```c++
asyncio::task::Task<void> func() {
    // ...
    const auto content = co_await asyncio::error::guard(readFile(path));
    // ...
}
```

- 通用的系统 `API` 返回值转为 `std::expected` 后，再用 `guard` 处理。

`Windows` 上返回值为 `BOOL` 的 `API` 即为通用的系统 `API` —— 返回 `TRUE` 表示成功，返回 `FALSE` 表示失败，失败时使用 `GetLastError` 获取错误码。

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

`UNIX` 上返回值为整形或指针的 `API` 即为通用的系统 `API` —— 返回 0 表示成功，返回 -1 表示失败，失败时使用 `errno` 获取错误码。

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

- 特殊的系统 `API`，手动判断错误并转为异常。

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

> 返回值为 `HANDLE` 的 `API`，有些失败时返回 `NULL`，有些失败时又返回 `INVALID_HANDLE_VALUE`，这也是 `zero::os::windows::expected` 不支持此类 `API` 的原因。

- 内部错误直接抛异常。

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

### 通用 `API` 代码
如果你需要编写一些通用的函数，作为库接口暴露给别人调用，那么就需要使用 `std::expected` 作为返回值。

- 返回值为 `std::expected<T, std::error_code>` 的函数使用 `EXPECT` 宏进行错误检查，出错时自动上抛。

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

- 通用的系统 `API` 返回值转为 `std::expected` 后，再用 `EXPECT` 处理。

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
    Z_CO_EXPECT(zero::os::windows::expected([&] {
        return CloseHandle(handle);
    }));
    // ...
}
```

- 特殊的系统 `API`，手动判断错误并转为 `std::error_code`。

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

- 内部错误返回自定义错误码。

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

# 错误处理

`asyncio` 的绝大多数 `API` 都返回 [`std::expected`](https://en.cppreference.com/w/cpp/utility/expected)，它类似于 `Rust` 的 `Result`。

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

## 自定义错误码

标准库提供的错误种类当然远远不够，`asyncio` 中的众多 `API` 都需要返回自定义的错误，例如任务超时、任务被取消等等。

> 如果偷懒地使用 `ETIMEDOUT`，`ECANCELED` 来返回错误，那么上层接收到之后，错误的来源将无从得知，因为 `std::error_code` 的缺陷就是无法记录调用栈。

但如果我们自定义错误种类、错误消息，便能稍微缓解这一痛点。

```c++
fmt::print(stderr, "Error: {} ({})\n", ec.message(), ec);
```

当上层打印出 `error code` 时，终端可能会显示：

> Error: Task was cancelled (asyncio::task:1)

从其中我们可以得知，错误种类是 `asyncio::task`，错误号是 1，错误信息是 `task was cancelled`，错误产生的原因我们也应该心中有数了。

不幸的是，自定义错误种类、错误码是一件极其繁琐的事情，我们需要定义自己的错误号枚举类型，还需要继承 [`std::error_category`](https://en.cppreference.com/w/cpp/error/error_category) 并重载部分虚函数。

项目前期，我曾尝试过一个个手写，但却难以坚持，不得已之下又只能求助于宏魔法。

```c++
// https://github.com/Hackerl/zero/blob/master/include/zero/error.h

// .h
namespace asyncio::task {
    Z_DEFINE_ERROR_CODE_EX(
        Error,
        "asyncio::task",
        CANCELLED, "Task was cancelled", std::errc::operation_canceled,
        CANCELLATION_NOT_SUPPORTED, "Task does not support cancellation", std::errc::operation_not_supported,
        LOCKED, "Task is locked", std::errc::resource_unavailable_try_again,
        CANCELLATION_TOO_LATE, "Operation will be done soon", Z_DEFAULT_ERROR_CONDITION
    )
}

Z_DECLARE_ERROR_CODE(asyncio::task::Error)

// .cpp
Z_DEFINE_ERROR_CATEGORY_INSTANCE(asyncio::task::Error)
```

上面就是 `asyncio` 中真实存在的代码，使用三个宏我们轻松定义了一个新的错误种类，还有错误码及对应的错误信息。

当某个任务被成功取消时，我们只需要 `return std::unexpected{task::Error::CANCELLED}`。

## 错误条件

在 `C++` 的错误体系中，`std::error_code` 代表具体产生的错误，我们当然可以比较两个 `error code` 是否相等来判断具体的错误原因。
但是如果我们只是想知道它是不是一个超时错误，或是任务被取消导致的，那应该怎么办呢？我们总不可能将它与所有可能出现的 `error code` 都比较一遍吧。
细心的你可能已经注意到了，在上一节自定义错误码的代码中，使用到了 `std::errc`：

```c++
Z_DEFINE_ERROR_CODE_EX(
    Error,
    "asyncio::task",
    CANCELLED, "Task was cancelled", std::errc::operation_canceled,
    CANCELLATION_NOT_SUPPORTED, "Task does not support cancellation", std::errc::operation_not_supported,
    LOCKED, "Task is locked", std::errc::resource_unavailable_try_again,
    CANCELLATION_TOO_LATE, "Operation will be done soon", Z_DEFAULT_ERROR_CONDITION
)
```

`std::error_condition` 代表一种错误条件，`std::errc` 就是标准库提供的与 `posix` 错误相对应的 `error condition`。
我们在自定义错误时，可以指定错误对应的错误条件，那么将 `error code` 与对应的 `error condtion` 比较时就会相等：

```c++
assert(std::error_code{asyncio::task::CANCELLED} == std::errc::operation_canceled);
```

当然，我们也可以自定义 `error condtion`：

```c++
namespace asyncio {
    Z_DEFINE_ERROR_CONDITION(
        IOError,
        "asyncio::io",
        UNEXPECTED_EOF, "Unexpected end of file"
    )
}
```

在 `asyncio` 中，我定义了一个 `IOError` 的错误条件，其中 `UNEXPECTED_EOF` 用来表示遇到了预料之外的 `EOF`。

而具体的 `error code`，则在实现具体的接口时定义：

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

`readExactly` 希望读取到准确数量的数据之后返回，如果读取到部分数据便遇到了 `EOF`，它便会返回 `asyncio::ReadExactlyError::UNEXPECTED_EOF` 错误。

上层的调用者可能不关心，也记不住那么多具体的错误码，他们只关心错误是否属于某一种情况：

```c++
assert(std::error_code{asyncio::ReadExactlyError::UNEXPECTED_EOF} == asyncio::IOError::UNEXPECTED_EOF);
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

## 错误追溯

错误追溯是一件让我头疼的事情，`error code` 无法携带任何动态数据用于溯源，它的错误描述通常也是一一映射的常量字符串。它也没有预留任何保留的字段，想要拓展它简直难如登天，而且我相信 `C++` 委员会也并不会去改变这一现状。
在我多次尝试无果之后，这一部分便只能暂且搁置，如果真的到了不得已而为之的那一天，我会选择用自定义的 `error code` 类型替换掉 `std::error_code`：

```c++
class ErrorCode : publibc std::error_code {
privete:
#ifndef NDEBUG
    std::stacktrace mStacktrace;
#endif
};
```

> 这并不是一件很难做的事情，只是我现在还没有立刻去做的理由。

## 错误传递

每一个可能失败的接口，我们都应该检查调用的结果。大多数时候，当我们检查到错误后，都会选择直接抛出给上层：

```c++
const auto result = func();

if (!result)
    return std::unexpected{result.error()};
```

但我不想在每个错误检查的地方都写上这么一长串代码，那样岂不是变成了 `Golang` 的模样？可是 `C++` 又没有 `Rust` 的问号语法糖，那该怎么办呢？

或许我们可以自己创造一个：

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

我们借助 `statement expression` 可以模拟出 `Rust` 问号语法糖：

```c++
const auto result1 = TRY(func());
const auto result2 = CO_TRY(co_await func1());
```

可惜，`MSVC` 并不支持该拓展语法，`GCC` 的相关支持也差强人意，于是我便只能退而求其次：

```c++
#define Z_EXPECT(...)                                                 \
    if (auto &&_result = __VA_ARGS__; !_result)                     \
        return std::unexpected{std::move(_result).error()}

#define Z_CO_EXPECT(...)                                              \
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

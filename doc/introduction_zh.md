# 介绍

## 为什么要创建这个项目？

我的工作主要是基于 `C++` 的客户端开发，大部分时间都免不了要和 `socket`、`HTTP`、`RPC` 这些打交道。
我使用过多种语言，偏爱异步编程的方式，当然，我指的并不是 `callback hell`。
`C++` 与网络编程似乎天生就相性不合，我尝试过不少的开源库，`libevent`、`libcurl` 以及 `gRPC` 等等，每当我坚定决心想要一条路走到黑时，却又不小心在眼花缭乱的回调里迷失了自我。

> `C++` 的网络异步编程难道就不能简单一些吗？

我想改变这一现状，所以不久之前我创建了 [`aio`](https://github.com/Hackerl/aio) 这个项目，一个基于 `promise` 的网络异步库。然而事与愿违，随着项目进展，我自回调的地狱中脱逃而出，却又陷入了生命周期的沼泽里无法自拔，`C++` 始终不是 `JavaScript`，无法做到那般洒脱自若。

> `JavaScript` 只要创建 `promise` 绑定回调就可以，可是 `C++` 要考虑的事情就很多了。

回调 `lambda` 捕获的变量的生命周期要如何管理？`promise` 绑定回调后代码运行出了 `scope`，要如何保证它依旧存活呢？在类方法中绑定回调，要如何捕获 `this`，是否要使用 `std::enable_shared_from_this` 呢？

看，这就是 `C++`，总是令人又爱又恨！

> 虽然 `C++` 总是为人诟病，但我依旧为之倾倒。

正在我一筹莫展想要知难而退之时，我突然想起了 `C++20` 的协程，我开始问自己，我为什么不试试这个新朋友呢？
所以我便创建了这个项目，我努力想让 `C++` 网络编程变得简单一些。即便最后以失败告终，但至少我也尝试过。

> 各种语言的协程库有利有弊、各有千秋，总是无法做到尽善尽美，`asyncio` 当然也不例外。

## 为什么要使用 `C++23` 进行开发？

- `C++23` 开始才加入了 `std::expected`。
- `C++23` 开始 `ranges` 标准库才算比较完善了。
- `C++23` 的 `deducing this` 能够解决 `lambda` 协程变量捕获悬空的问题（虽然有些编译器还未实现）。

我偏爱优雅、简单的代码，`C++20` 却处在一个不上不下十分尴尬的位置。许多组件的不完善，使得开发起来束手束脚，那我为何不干脆用 `C++23` 呢？

> 若无法卸下往日的枷锁，总是回头往后看，怎能尽情地奔跑起来呢？

## `asyncio` 是如何运作的？

`asyncio` 基于 `libuv` 构建，单线程上运行 `Event Loop`，由事件回调触发协程的恢复。
`asyncio` 的部分功能都是对 `libuv` 的简单封装，只不过是将回调转为了协程。

> 如果我比别人看得更远，那是因为我站在巨人的肩上。

值得一提的是，几乎所有底层的代码都需要使用 `Event Loop`，所以为了避免层层传递，我将它放入了 `thread local` 变量中。

```c++
namespace asyncio {
    std::shared_ptr<EventLoop> getEventLoop();
}
```

只要处在 `Event Loop` 线程中，便能获取它。

> 这何尝又不是一种 `context` 呢？

## 项目有何优点？

- 简单、精巧的代码
- 灵活、优雅的子任务管理
- 借鉴自多种语言，易于使用的 `API`
- 借鉴自多种语言，设计优良的接口
- 简单直接的任务取消机制
- 基于线程、线程池可以轻松融合同步代码

## 目前提供哪些功能？

- Timer
- Thread
- Channel
- Signal
- Buffer Reader/Writer
- Filesystem
- Process
- Synchronization Primitives
- DNS
- UDP Socket、TCP/Unix/NamedPipe Stream
- SSL/TLS
- HTTP Client
- WebSocket Client

> 暂未实现 `HTTP` 服务端的相关功能，原因是其中细节繁多，个人精力有限。

即便其中缺少你想要的功能，也并不要紧，只要使用 `thread` 模块就可以轻松集成第三方组件：

```c++
class Server {
public:
    asyncio::task::Task<void, std::error_code> run();

private:
    httplib::Server mServer;
};


asyncio::task::Task<void, std::error_code> agent::local::Server::run() {
    mServer.Get(
        "/",
        [](const httplib::Request &, httplib::Response &response) {
            response.set_content("Hello World!", "text/plain");
        }
    );

    co_return co_await asyncio::toThread(
        [this]() -> std::expected<void, std::error_code> {
            mServer.listen(mConfig.ip, mConfig.port);
            return {};
        },
        [this](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
            mServer.stop();
            return {};
        }
    );
}
```

> 打破枷锁并不意味地抛弃过往，若无过去，怎有未来，那些伟大的开源项目都是宝贵的财富，怎能弃之如敝屣。

## 未来有何规划？

- 完善文档
- 完善测试

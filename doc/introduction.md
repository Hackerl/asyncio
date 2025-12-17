# Introduction

## Why create this project?

My work mainly revolves around `C++` client development, where I often find myself dealing with `sockets`, `HTTP`, and `RPC` protocols. I have worked with various programming languages, but I prefer asynchronous programming, though not the kind that leads to "callback hell."

`C++` and network programming seem inherently incompatible. I have tried several open-source libraries like `libevent`, `libcurl`, and `gRPC`. Every time I decide to stick with one solution, I end up getting lost in the overwhelming maze of callbacks.

> Can't `C++` network asynchronous programming be simpler?

I wanted to change this, so not long ago, I created the [`aio`](https://github.com/Hackerl/aio) project, an asynchronous network library based on `promises`. However, as the project progressed, I escaped from callback hell, only to fall into the swamp of managing lifecycles. `C++` is just not as carefree as `JavaScript`—it has many more considerations to account for.

> In `JavaScript`, you can simply create a `promise` and bind callbacks, but in `C++`, there are so many things to consider.

How should the lifecycle of variables captured by callback lambdas be managed? Once a `promise` binds a callback and the code runs out of scope, how can we ensure the callback remains alive? When binding callbacks in class methods, how do we capture `this`? Should we use `std::enable_shared_from_this`?

See, that's `C++`—it's both frustrating and endearing!

> Despite the criticisms, I am still enamored with `C++`.

Just when I was about to give up, I suddenly remembered `C++20` coroutines, and I started asking myself, why not give this new feature a try? So, I created this project to make `C++` network programming simpler. Even if it ultimately fails, at least I gave it a shot.

> Coroutine libraries in various languages have their pros and cons, each with its strengths and weaknesses. `asyncio` is no exception.

## Why use `C++23` for development?

- `C++23` introduced `std::expected`.
- `C++23` also made the `ranges` standard library quite complete.
- `C++23`'s `deducing this` feature solves the issue of dangling coroutine variable captures in lambdas (although some compilers have yet to implement it).

I prefer elegant, simple code, and `C++20` is caught in an awkward middle ground. Many components are incomplete, which hinders development. So, why not just use `C++23`?

> If you can't shake off the shackles of the past and keep looking back, how can you run freely toward the future?

## How does `asyncio` work?

`asyncio` is built on top of `libuv`, running a single-threaded `Event Loop` that triggers coroutine resumes via event callbacks. Some features of `asyncio` are just simple wrappers around `libuv`, except that the callbacks are converted into coroutines.

> If I have seen further than others, it is because I stand on the shoulders of giants.

It is worth noting that almost all low-level code requires the use of an `Event Loop`. To avoid passing it around repeatedly, I have placed it in a `thread local` variable.

```c++
namespace asyncio {
    std::shared_ptr<EventLoop> getEventLoop();
}
```

As long as you are in the `Event Loop` thread, you can retrieve it.

> Isn't this just another form of `context`?

## What are the advantages of this project?

- Simple, elegant code
- Flexible, elegant sub-task management
- User-friendly `API`, inspired by multiple languages
- Well-designed interfaces inspired by multiple languages
- Simple and direct task cancellation mechanism
- Easily integrates with synchronous code using threads and thread pools

## What features are currently available?

- Timer
- Thread
- Channel
- Signal
- Buffer Reader/Writer
- Filesystem
- Process
- Synchronization Primitives
- DNS
- UDP Socket, TCP/Unix/NamedPipe Stream
- SSL/TLS
- HTTP Client
- WebSocket Client

> The `HTTP` server functionality has not yet been implemented due to its numerous details and limited personal resources.

Even if the functionality you need is missing, that's okay. You can easily integrate third-party components using the `thread` module:

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

> Breaking the chains does not mean abandoning the past. Without the past, there would be no future. Those great open-source projects are precious treasures, which should not be discarded.

## What are the future plans?

- Improve documentation
- Improve testing

<!-- Improved compatibility of back to top link: See: https://github.com/othneildrew/Best-README-Template/pull/73 -->
<a name="readme-top"></a>
<!--
*** Thanks for checking out the Best-README-Template. If you have a suggestion
*** that would make this better, please fork the repo and create a pull request
*** or simply open an issue with the tag "enhancement".
*** Don't forget to give the project a star!
*** Thanks again! Now go create something AMAZING! :D
-->



<!-- PROJECT SHIELDS -->
<!--
*** I'm using markdown "reference style" links for readability.
*** Reference links are enclosed in brackets [ ] instead of parentheses ( ).
*** See the bottom of this document for the declaration of the reference variables
*** for contributors-url, forks-url, etc. This is an optional, concise syntax you may use.
*** https://www.markdownguide.org/basic-syntax/#reference-style-links
-->
[![Contributors][contributors-shield]][contributors-url]
[![Forks][forks-shield]][forks-url]
[![Stargazers][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]
[![Apache 2.0 License][license-shield]][license-url]



<!-- PROJECT LOGO -->
<br />
<div align="center">

<h3 align="center">asyncio</h3>

  <p align="center">
    C++23 coroutine network framework
    <br />
    <a href="https://github.com/Hackerl/asyncio/tree/master/doc"><strong>Explore the docs »</strong></a>
    <br />
    <br />
    <a href="https://github.com/Hackerl/asyncio/tree/master/sample">View Demo</a>
    ·
    <a href="https://github.com/Hackerl/asyncio/issues">Report Bug</a>
    ·
    <a href="https://github.com/Hackerl/asyncio/issues">Request Feature</a>
  </p>
</div>



<!-- TABLE OF CONTENTS -->
<details>
  <summary>Table of Contents</summary>
  <ol>
    <li>
      <a href="#about-the-project">About The Project</a>
      <ul>
        <li><a href="#built-with">Built With</a></li>
      </ul>
    </li>
    <li>
      <a href="#getting-started">Getting Started</a>
      <ul>
        <li><a href="#prerequisites">Prerequisites</a></li>
        <li><a href="#build">Build</a></li>
        <li><a href="#installation">Installation</a></li>
      </ul>
    </li>
    <li><a href="#usage">Usage</a></li>
    <li><a href="#roadmap">Roadmap</a></li>
    <li><a href="#contributing">Contributing</a></li>
    <li><a href="#license">License</a></li>
    <li><a href="#contact">Contact</a></li>
    <li><a href="#acknowledgments">Acknowledgments</a></li>
  </ol>
</details>



<!-- ABOUT THE PROJECT -->
## About The Project

Based on the `libuv` event loop, use C++20 stackless `coroutines` to implement network components, and provide `channel` to send and receive data between tasks.

`asyncio` might be better than existing coroutine network libraries in the following ways:
- A unified error handling method based on `std::expected<T, std::error_code>`, but also supports exception handling.
- A simple and direct cancellation method similar to `Python`'s `asyncio` - `task.cancel()`.
- Lessons learned from `JavaScript`'s `Promise.all`, `Promise.any`, `Promise.race`, etc., subtask management methods.
- Lessons learned from `Golang`'s `WaitGroup` dynamic task management groups.
- Built-in call stack tracing allows for better debugging and analysis.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



### Built With

* [![CMake][CMake]][CMake-url]
* [![vcpkg][vcpkg]][vcpkg-url]
* [![C++23][C++23]][C++23-url]

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- GETTING STARTED -->
## Getting Started

### Prerequisites

Required compiler:
* GCC >= 15
* LLVM >= 18
* MSVC >= 19.38

Export environment variables:
* VCPKG_ROOT
* ANDROID_NDK_HOME(Android)

### Build

```sh
cmake --workflow --preset debug
```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

### Installation

Install `asyncio` from the [`vcpkg` private registry](https://github.com/Hackerl/vcpkg-registry):

1. Create a `vcpkg-configuration.json` file in the project root directory:

   ```json
   {
     "registries": [
       {
         "kind": "git",
         "repository": "https://github.com/Hackerl/vcpkg-registry",
         "baseline": "60d2c042c7660ce938bb242e3fa370cb650427c7",
         "reference": "pre",
         "packages": [
           "asyncio",
           "zero"
         ]
       }
     ]
   }
   ```

   > The `baseline` defines the minimum version of `asyncio` that will be installed. The one used above might be outdated, so please update it as necessary.

2. Create a `vcpkg.json` file in the project root directory:

   ```json
   {
     "name": "project name",
     "version-string": "1.0.0",
     "builtin-baseline": "6b3172d1a7be062b3d0278369aac9a0258cefc65",
     "dependencies": [
       "asyncio"
     ]
   }
   ```

3. Add the following to the `CMakeLists.txt` file:

   ```cmake
   find_package(asyncio CONFIG REQUIRED)
   target_link_libraries(custom_target PRIVATE asyncio::asyncio-main)
   ```



<!-- USAGE EXAMPLES -->
## Usage

I'm using a typical TCP echo server to demonstrate the features of asyncio as much as possible.

```c++
#include <asyncio/net/stream.h> // Streaming network components
#include <asyncio/thread.h> // Thread and thread pool components
#include <asyncio/signal.h> // Signal component
#include <asyncio/time.h> // Time component
#include <zero/cmdline.h> // Command line parsing component
#include <zero/formatter.h> // Formatting utilities
#include <zero/os/resource.h> // Operating system fd/handle wrapper

#ifdef _WIN32
#include <zero/os/windows/error.h> // Windows API call wrapper
#endif

namespace {
    // Receive event or signal, print the task's call stack.
    // For the top-level task, complex subtasks will branch out and form a tree.
    asyncio::task::Task<void> tracing(const auto &task) {
#ifdef _WIN32
        const auto handle = CreateEventA(nullptr, false, false, "Global\\AsyncIOBacktraceEvent");

        if (!handle)
            throw zero::error::SystemError{
                std::error_code{static_cast<int>(GetLastError()), std::system_category()}
            };

        const zero::os::Resource event{handle};

        while (true) {
            bool cancelled{false};

            // `WaitForSingleObject` cannot be integrated into EventLoop, so we use a separate thread to call it,
            // and a custom cancellation function allows it to be seamlessly integrated into coroutine management.
            co_await asyncio::toThread(
                [&, &cancelled = std::as_const(cancelled)] {
                    if (WaitForSingleObject(*event, INFINITE) != WAIT_OBJECT_0)
                        throw zero::error::SystemError{static_cast<int>(GetLastError()), std::system_category()};

                    if (cancelled)
                        throw zero::error::SystemError{asyncio::task::Error::CANCELLED};

                    return;
                },
                [&](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
                    cancelled = true;
                    return zero::os::windows::expected([&] {
                        return SetEvent(*event);
                    });
                }
            );

            // Print the task's formatted call stack
            fmt::print(stderr, "{}\n", task.trace());
        }
#else
        // On UNIX, the Signal component can be used directly.
        auto signal = asyncio::Signal::make();

        while (true) {
            zero::error::guard(co_await signal.on(SIGUSR1));
            // Print the task's formatted call stack
            fmt::print(stderr, "{}\n", task.trace());
        }
#endif
    }

    asyncio::task::Task<void> doSomething() {
        using namespace std::chrono_literals;

        while (true) {
            zero::error::guard(co_await asyncio::sleep(1s));
            // Trace back the coroutine call stack
            fmt::print("Do some thing: {}\n", fmt::join(co_await asyncio::task::backtrace, "\n"));
        }
    }

    asyncio::task::Task<void> handle(asyncio::net::TCPStream stream) {
        const auto address = zero::error::guard(stream.remoteAddress());
        fmt::print("Connection: {}\n", address);

        while (true) {
            std::string message;
            message.resize(1024);

            const auto n = zero::error::guard(co_await stream.read(std::as_writable_bytes(std::span{message})));

            if (n == 0)
                break;

            message.resize(n);

            fmt::print("Receive message: {}\n", message);
            zero::error::guard(co_await stream.writeAll(std::as_bytes(std::span{message})));
        }
    }

    asyncio::task::Task<void> serve(asyncio::net::TCPListener listener) {
        std::expected<void, std::error_code> result;

        // By adding each dynamic task to a `TaskGroup`,
        // we can cancel them all at once and wait for them during graceful shutdown,
        // ensuring that no resources or subtasks are leaked.
        asyncio::task::TaskGroup group;

        while (true) {
            auto stream = co_await listener.accept();

            if (!stream) {
                result = std::unexpected{stream.error()};
                break;
            }

            auto task = handle(*std::move(stream));

            group.add(task);

            // Since the `TaskGroup` doesn't care about the results of the subtasks, we can use future to bind callbacks.
            // Callback binding is very flexible, just like JavaScript's Promise.
            task.future().fail([](const auto &e) {
                fmt::print(stderr, "Unhandled exception: {}\n", e);
            });
        }

        // This function waits for all tasks in the `TaskGroup`.
        // When the parent task is canceled, all tasks in the group will be automatically canceled here and will wait for them to complete.
        co_await group;
        zero::error::guard(std::move(result));
    }
}

asyncio::task::Task<void> asyncMain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<std::string>("ip", "IP address to bind");
    cmdline.add<std::uint16_t>("port", "Port number to listen on");

    cmdline.parse(argc, argv);

    const auto ip = cmdline.get<std::string>("ip");
    const auto port = cmdline.get<std::uint16_t>("port");

    auto listener = zero::error::guard(asyncio::net::TCPListener::listen(ip, port));
    auto signal = asyncio::Signal::make();

    // This is the main task of our program.
    auto task = race(
        // A TCP server was started, along with a `doSomething` task to do something else.
        // They are aggregated by `all`, just like `Promise.all` in JavaScript, where failure is returned if either task fails, and the remaining tasks are canceled.
        all(
            serve(std::move(listener)),
            doSomething()
        ),
        // We wait for the `SIGINT` signal to gracefully shut down.
        // `race` will use the result of the task that completes fastest, so when the signal arrives,
        // the task is complete, `race` returns success and cancels the remaining subtasks.
        asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
            zero::error::guard(co_await signal.on(SIGINT));
        })
    );

    // Debugging coroutines is always difficult, so we use the built-in traceback functionality of `asyncio` to assist us.
    co_await race(
        task,
        tracing(task)
    );
}
```

> Start the server with `./server 0.0.0.0 8000`, and gracefully exit by pressing `ctrl + c` in the terminal.
> You can also send signal or event to trace the call tree.

_For more examples, please refer to the [Documentation](https://github.com/Hackerl/asyncio/tree/master/doc)_

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- ROADMAP -->
## Roadmap

- [ ] HTTP Server

See the [open issues](https://github.com/Hackerl/asyncio/issues) for a full list of proposed features (and known issues).

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- CONTRIBUTING -->
## Contributing

Contributions are what make the open source community such an amazing place to learn, inspire, and create. Any contributions you make are **greatly appreciated**.

If you have a suggestion that would make this better, please fork the repo and create a pull request. You can also simply open an issue with the tag "enhancement".
Don't forget to give the project a star! Thanks again!

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- LICENSE -->
## License

Distributed under the Apache 2.0 License. See `LICENSE` for more information.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- CONTACT -->
## Contact

Hackerl - [@Hackerl](https://github.com/Hackerl) - patteliu@gmail.com

Project Link: [https://github.com/Hackerl/asyncio](https://github.com/Hackerl/asyncio)

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- ACKNOWLEDGMENTS -->
## Acknowledgments

* [libuv](https://libuv.org)
* [curl](https://curl.se)
* [treehh](https://github.com/kpeeters/tree.hh)
* [nlohmann-json](https://json.nlohmann.me)
* [OpenSSL](https://www.openssl.org)
* [Catch2](https://github.com/catchorg/Catch2)

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- MARKDOWN LINKS & IMAGES -->
<!-- https://www.markdownguide.org/basic-syntax/#reference-style-links -->
[contributors-shield]: https://img.shields.io/github/contributors/Hackerl/asyncio.svg?style=for-the-badge
[contributors-url]: https://github.com/Hackerl/asyncio/graphs/contributors
[forks-shield]: https://img.shields.io/github/forks/Hackerl/asyncio.svg?style=for-the-badge
[forks-url]: https://github.com/Hackerl/asyncio/network/members
[stars-shield]: https://img.shields.io/github/stars/Hackerl/asyncio.svg?style=for-the-badge
[stars-url]: https://github.com/Hackerl/asyncio/stargazers
[issues-shield]: https://img.shields.io/github/issues/Hackerl/asyncio.svg?style=for-the-badge
[issues-url]: https://github.com/Hackerl/asyncio/issues
[license-shield]: https://img.shields.io/github/license/Hackerl/asyncio.svg?style=for-the-badge
[license-url]: https://github.com/Hackerl/asyncio/blob/master/LICENSE
[CMake]: https://img.shields.io/badge/CMake-000000?style=for-the-badge&logo=cmake&logoColor=FF3E00
[CMake-url]: https://cmake.org
[vcpkg]: https://img.shields.io/badge/vcpkg-000000?style=for-the-badge&logo=v&logoColor=61DAFB
[vcpkg-url]: https://vcpkg.io
[C++23]: https://img.shields.io/badge/C++23-000000?style=for-the-badge&logo=cplusplus&logoColor=4FC08D
[C++23-url]: https://en.cppreference.com/w/cpp/20
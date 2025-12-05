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
* GCC >= 14
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
         "baseline": "aa3865a8ad99b5265c824b0b550fc71bea9a90b1",
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

### HTTP Client

Using the `HTTP Client` in a `C++` project has never been easier:

```c++
#include <asyncio/http/request.h>

asyncio::task::Task<void, std::error_code> asyncMain(const int argc, char *argv[]) {
    const auto url = asyncio::http::URL::from("https://www.google.com");
    CO_EXPECT(url);

    auto requests = asyncio::http::Requests::make();
    CO_EXPECT(requests);

    auto response = co_await requests->get(*url);
    CO_EXPECT(response);

    const auto content = co_await response->string();
    CO_EXPECT(content);

    fmt::print("{}", *content);
    co_return {};
}
```

> We use `asyncMain` instead of `main` as the entry point, and `CO_EXPECT` is used to check for errors and throw them upwards.

### TCP

#### Server

```c++
#include <asyncio/net/stream.h>
#include <asyncio/signal.h>
#include <zero/cmdline.h>

asyncio::task::Task<void, std::error_code> handle(asyncio::net::TCPStream stream) {
    const auto address = stream.remoteAddress();
    CO_EXPECT(address);

    fmt::print("connection[{}]\n", *address);

    while (true) {
        std::string message;
        message.resize(1024);

        const auto n = co_await stream.read(std::as_writable_bytes(std::span{message}));
        CO_EXPECT(n);

        if (*n == 0)
            break;

        message.resize(*n);

        fmt::print("receive message: {}\n", message);
        CO_EXPECT(co_await stream.writeAll(std::as_bytes(std::span{message})));
    }

    co_return {};
}

asyncio::task::Task<void, std::error_code> serve(asyncio::net::TCPListener listener) {
    std::expected<void, std::error_code> result;
    asyncio::task::TaskGroup group;

    while (true) {
        auto stream = co_await listener.accept();

        if (!stream) {
            result = std::unexpected{stream.error()};
            break;
        }

        auto task = handle(*std::move(stream));

        group.add(task);
        task.future().fail([](const auto &ec) {
            fmt::print(stderr, "unhandled error: {} ({})\n", ec.message(), ec);
        });
    }

    co_await group;
    co_return result;
}

asyncio::task::Task<void, std::error_code> asyncMain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<std::uint16_t>("port", "remote port");

    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<std::uint16_t>("port");

    auto listener = asyncio::net::TCPListener::listen(host, port);
    CO_EXPECT(listener);

    auto signal = asyncio::Signal::make();
    CO_EXPECT(signal);

    co_return co_await race(
        serve(*std::move(listener)),
        signal->on(SIGINT).transform([](const int) {
        })
    );
}
```

> Start the server with `./server 127.0.0.1 8000`, and gracefully exit by pressing `ctrl + c` in the terminal.

#### Client

```c++
#include <asyncio/net/stream.h>
#include <asyncio/time.h>
#include <zero/cmdline.h>

asyncio::task::Task<void, std::error_code> asyncMain(const int argc, char *argv[]) {
    using namespace std::chrono_literals;
    using namespace std::string_view_literals;

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<std::uint16_t>("port", "remote port");

    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<std::uint16_t>("port");

    auto stream = co_await asyncio::net::TCPStream::connect(host, port);
    CO_EXPECT(stream);

    while (true) {
        CO_EXPECT(co_await stream->writeAll(std::as_bytes(std::span{"hello world"sv})));

        std::string message;
        message.resize(1024);

        const auto n = co_await stream->read(std::as_writable_bytes(std::span{message}));
        CO_EXPECT(n);

        if (*n == 0)
            break;

        message.resize(*n);

        fmt::print("receive message: {}\n", message);
        co_await asyncio::sleep(1s);
    }

    co_return {};
}
```

> Connect to the server with `./client 127.0.0.1 8000`.

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
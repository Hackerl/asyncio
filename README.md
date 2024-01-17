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
    C++20 coroutine network framework
    <br />
    <a href="https://github.com/Hackerl/asyncio/wiki"><strong>Explore the docs »</strong></a>
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

Based on the `libevent` event loop, use C++20 stackless `coroutines` to implement events and network basic components, and provide `channel` to send and receive data between tasks.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



### Built With

* [![CMake][CMake]][CMake-url]
* [![vcpkg][vcpkg]][vcpkg-url]
* [![C++20][C++20]][C++20-url]

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- GETTING STARTED -->
## Getting Started

Due to many dependencies, it is not recommended to install manually, you should use `vcpkg`.

### Prerequisites

Required compiler:
* GCC >= 13
* LLVM >= 16
* MSVC >= 19.38

Export environment variables:
* VCPKG_INSTALLATION_ROOT
* ANDROID_NDK_HOME(Android)

### Build

* Linux
  ```sh
  mkdir -p build && cmake -B build -DCMAKE_TOOLCHAIN_FILE="${VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake" && cmake --build build -j$(nproc)
  ```

* Android
  ```sh
  # set "ANDROID_PLATFORM" for dependencies installed by vcpkg: echo 'set(VCPKG_CMAKE_SYSTEM_VERSION 24)' >> "${VCPKG_INSTALLATION_ROOT}/triplets/community/arm64-android.cmake"
  mkdir -p build && cmake -B build -DCMAKE_TOOLCHAIN_FILE="${VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake" -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" -DVCPKG_TARGET_TRIPLET=arm64-android -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 && cmake --build build -j$(nproc)
  ```

* Windows(Developer PowerShell)
  ```sh
  mkdir -p build && cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" && cmake --build build -j $env:NUMBER_OF_PROCESSORS
  ```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

### Installation
You can use `CMake` to compile and install based on the source code, or use `CMake` + `vcpkg` directly.

* CMakeLists.txt
  ```cmake
  find_package(asyncio CONFIG REQUIRED)
  add_executable(demo main.cpp)
  target_link_libraries(demo PRIVATE asyncio::asyncio)
  ```

* vcpkg-configuration.json
  ```json
  {
    "registries": [
      {
        "kind": "git",
        "repository": "https://github.com/Hackerl/vcpkg-registry",
        "baseline": "4abdfe14cbaee3e51d28a169061b7d3e54dbcc37",
        "packages": [
          "asyncio",
          "zero"
        ]
      }
    ]
  }
  ```

* vcpkg.json
  ```json
  {
    "name": "demo",
    "version-string": "1.0.0",
    "builtin-baseline": "c9e2aa851e987698519f58518aa16564af3a85ab",
    "dependencies": [
      {
        "name": "asyncio",
        "version>=": "1.0.1"
      }
    ]
  }
  ```



<!-- USAGE EXAMPLES -->
## Usage

### TCP/TLS

* Basic

  ```cpp
  asyncio::run([&]() -> zero::async::coroutine::Task<void> {
      auto buffer = co_await asyncio::net::stream::connect(host, port);

      if (!buffer) {
          LOG_ERROR("stream buffer connect failed{}]", buffer.error());
          co_return;
      }

      while (true) {
          std::string message = "hello world\r\n";
          auto result = co_await buffer->writeAll(std::as_bytes(std::span{message}));

          if (!result) {
              LOG_ERROR("stream buffer drain failed[{}]", result.error());
              break;
          }

          auto line = co_await buffer->readLine();

          if (!line) {
              LOG_ERROR("stream buffer read line failed[{}]", line.error());
              break;
          }

          LOG_INFO("receive message[{}]", *line);
          co_await asyncio::sleep(1s);
      }
  });
  ```

* TLS

  ```cpp
  asyncio::run([&]() -> zero::async::coroutine::Task<void> {
      asyncio::net::ssl::Config config = {
              .insecure = insecure,
              .server = false
      };

      if (ca)
          config.ca = *ca;

      if (cert)
          config.cert = *cert;

      if (privateKey)
          config.privateKey = *privateKey;

      auto context = asyncio::net::ssl::newContext(config);

      if (!context) {
          LOG_ERROR("create ssl context failed[{}]", context.error());
          co_return;
      }

      auto buffer = co_await asyncio::net::ssl::stream::connect(*context, host, port);

      if (!buffer) {
          LOG_ERROR("stream buffer connect failed[{}]", buffer.error());
          co_return;
      }

      while (true) {
          std::string message = "hello world\r\n";
          auto result = co_await buffer->writeAll(std::as_bytes(std::span{message}));

          if (!result) {
              LOG_ERROR("stream buffer drain failed[{}]", result.error());
              break;
          }

          auto line = co_await buffer->readLine();

          if (!line) {
              LOG_ERROR("stream buffer read line failed[{}]", line.error());
              break;
          }

          LOG_INFO("receive message[{}]", *line);
          co_await asyncio::sleep(1s);
      }
  });
  ```

### Worker

* Basic

  ```cpp
  asyncio::run([]() -> zero::async::coroutine::Task<void> {
      co_await asyncio::toThread([]() -> tl::expected<void, std::error_code> {
          auto tp = std::chrono::system_clock::now();
          std::this_thread::sleep_for(50ms);
          REQUIRE(std::chrono::system_clock::now() - tp > 50ms);
          return {};
      });
  });
  ```

* Throw error

  ```cpp
  asyncio::run([]() -> zero::async::coroutine::Task<void> {
      auto result = co_await asyncio::toThread([]() -> tl::expected<void, std::error_code> {
          std::this_thread::sleep_for(10ms);
          return tl::unexpected(make_error_code(std::errc::bad_message));
      });

      REQUIRE(!result);
      REQUIRE(result.error() == std::errc::bad_message);
  });
  ```

### Channel

* Basic

  ```cpp
  asyncio::run([]() -> zero::async::coroutine::Task<void> {
      auto channel = std::make_shared<asyncio::Channel<int>>(100);

      co_await zero::async::coroutine::allSettled(
              [](auto channel) -> zero::async::coroutine::Task<void, std::error_code> {
                  tl::expected<void, std::error_code> result;

                  for (int i = 0; i < 1000; i++) {
                      auto res = co_await channel->send(i);

                      if (!res) {
                          result = tl::unexpected(res.error());
                          break;
                      }

                      co_await asyncio::sleep(1s);
                  }

                  co_return result;
              }(channel),
              [](auto channel) -> zero::async::coroutine::Task<void, std::error_code> {
                  tl::expected<void, std::error_code> result;

                  while (true) {
                      auto res = co_await channel->receive();

                      if (!res) {
                          result = tl::unexpected(res.error());
                          break;
                      }

                      // do something
                  }

                  co_return result;
              }(channel)
      );
  });
  ```

* Concurrent

  ```cpp
  asyncio::run([]() -> zero::async::coroutine::Task<void> {
      auto channel = std::make_shared<asyncio::Channel<int>>(100);

      co_await zero::async::coroutine::allSettled(
              [](auto channel) -> zero::async::coroutine::Task<void, std::error_code> {
                  tl::expected<void, std::error_code> result;

                  for (int i = 0; i < 1000; i++) {
                      auto res = co_await channel->send(i);

                      if (!res) {
                          result = tl::unexpected(res.error());
                          break;
                      }

                      co_await asyncio::sleep(1s);
                  }

                  co_return result;
              }(channel),
              asyncio::toThread([=]() -> tl::expected<void, std::error_code> {
                  tl::expected<void, std::error_code> result;

                  while (true) {
                      auto res = channel->receiveSync();

                      if (!res) {
                          result = tl::unexpected(res.error());
                          break;
                      }

                      // do something that takes a long time
                  }

                  return result;
              })
      );
  });
  ```

_For more examples, please refer to the [Documentation](https://github.com/Hackerl/asyncio/wiki)_

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- ROADMAP -->
## Roadmap

- [ ] HTTP WebSocket
- [ ] Asynchronous Filesystem
  - [ ] Linux AIO
  - [ ] Windows IOCP
  - [ ] POSIX AIO

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

* [libevent](https://libevent.org)
* [tl-expected](https://github.com/TartanLlama/expected)

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
[vcpkg]: https://img.shields.io/badge/vcpkg-000000?style=for-the-badge&logo=microsoft&logoColor=61DAFB
[vcpkg-url]: https://vcpkg.io
[C++20]: https://img.shields.io/badge/C++20-000000?style=for-the-badge&logo=cplusplus&logoColor=4FC08D
[C++20-url]: https://en.cppreference.com/w/cpp/20
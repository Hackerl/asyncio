#include <asyncio/fs/framework.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <zero/defer.h>
#include <filesystem>

#if __unix__ || __APPLE__
#include <fcntl.h>
#include <unistd.h>
#endif

TEST_CASE("asynchronous filesystem framework", "[filesystem framework]") {
#if __unix__ || __APPLE__
    SECTION("posix aio") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            auto framework = asyncio::fs::makePosixAIO(asyncio::getEventLoop().get());
            REQUIRE(framework);

            auto path = std::filesystem::temp_directory_path() / "asyncio-fs-file";
            int fd = open(path.string().c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
            REQUIRE(fd > 0);
            DEFER(close(fd));
            DEFER(std::filesystem::remove(path));

            auto content = {
                    std::byte{'h'},
                    std::byte{'e'},
                    std::byte{'l'},
                    std::byte{'l'},
                    std::byte{'o'}
            };

            auto result = co_await framework->write(fd, 0, content);
            REQUIRE(result);
            REQUIRE(*result == content.size());

            std::byte data[5];
            result = co_await framework->read(fd, 0, data);
            REQUIRE(result);
            REQUIRE(*result == sizeof(data));
            REQUIRE(std::equal(data, data + sizeof(data), content.begin()));
        });
    }
#endif

#ifdef _WIN32
    SECTION("IOCP") {
        SECTION("normal") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto framework = asyncio::fs::makeIOCP(asyncio::getEventLoop().get());
                REQUIRE(framework);

                auto path = std::filesystem::temp_directory_path() / "asyncio-fs-file";
                HANDLE handle = CreateFileA(
                        path.string().c_str(),
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        nullptr,
                        CREATE_ALWAYS,
                        FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OVERLAPPED,
                        nullptr
                );
                REQUIRE(handle != INVALID_HANDLE_VALUE);
                DEFER(CloseHandle(handle));

                auto fd = (asyncio::FileDescriptor) handle;
                auto result = framework->associate(fd);
                REQUIRE(result);

                auto content = {
                        std::byte{'h'},
                        std::byte{'e'},
                        std::byte{'l'},
                        std::byte{'l'},
                        std::byte{'o'}
                };

                auto res = co_await framework->write(fd, 0, content);
                REQUIRE(res);
                REQUIRE(*res == content.size());

                std::byte data[5];
                res = co_await framework->read(fd, 0, data);
                REQUIRE(res);
                REQUIRE(*res == sizeof(data));
                REQUIRE(std::equal(data, data + sizeof(data), content.begin()));
            });
        }

        SECTION("cancel") {
            asyncio::run([]() -> zero::async::coroutine::Task<void> {
                auto framework = asyncio::fs::makeIOCP(asyncio::getEventLoop().get());
                REQUIRE(framework);

                HANDLE pipe = CreateNamedPipeA(
                        R"(\\.\pipe\asyncio)",
                        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
                        PIPE_UNLIMITED_INSTANCES,
                        4096,
                        4096,
                        0,
                        nullptr
                );
                REQUIRE(pipe != INVALID_HANDLE_VALUE);
                DEFER(CloseHandle(pipe));

                HANDLE handle = CreateFile(
                        R"(\\.\pipe\asyncio)",
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        nullptr,
                        OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED,
                        nullptr
                );
                REQUIRE(handle != INVALID_HANDLE_VALUE);
                DEFER(CloseHandle(handle));

                auto fd = (asyncio::FileDescriptor) pipe;
                auto result = framework->associate(fd);
                REQUIRE(result);

                std::byte data[5];
                auto task = framework->read(fd, 0, data);

                task.cancel();
                co_await task;
                auto res = task.result();

                REQUIRE(!res);
                REQUIRE(res.error() == std::errc::operation_canceled);
            });
        }
    }
#endif
}
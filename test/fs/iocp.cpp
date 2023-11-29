#include <asyncio/fs/iocp.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <zero/defer.h>
#include <filesystem>

TEST_CASE("IOCP", "[filesystem framework]") {
    SECTION("normal") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            const auto eventLoop = asyncio::getEventLoop();
            REQUIRE(eventLoop);

            auto framework = asyncio::fs::makeIOCP();
            REQUIRE(framework);

            const auto path = std::filesystem::temp_directory_path() / "asyncio-fs-file";
            const auto handle = CreateFileA(
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

            const auto fd = reinterpret_cast<asyncio::FileDescriptor>(handle);
            const auto result = framework->associate(fd);
            REQUIRE(result);

            constexpr std::array content = {
                std::byte{'h'},
                std::byte{'e'},
                std::byte{'l'},
                std::byte{'l'},
                std::byte{'o'}
            };

            auto res = co_await framework->write(eventLoop, fd, 0, content);
            REQUIRE(res);
            REQUIRE(*res == content.size());

            std::byte data[5];
            res = co_await framework->read(eventLoop, fd, 0, data);
            REQUIRE(res);
            REQUIRE(*res == sizeof(data));
            REQUIRE(std::equal(data, data + sizeof(data), content.begin()));
        });
    }

    SECTION("cancel") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            const auto eventLoop = asyncio::getEventLoop();
            REQUIRE(eventLoop);

            auto framework = asyncio::fs::makeIOCP();
            REQUIRE(framework);

            const auto pipe = CreateNamedPipeA(
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

            const auto handle = CreateFile(
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

            const auto fd = reinterpret_cast<asyncio::FileDescriptor>(pipe);
            const auto result = framework->associate(fd);
            REQUIRE(result);

            std::byte data[5];
            auto task = framework->read(eventLoop, fd, 0, data);

            task.cancel();
            co_await task;
            const auto res = task.result();

            REQUIRE(!res);
            REQUIRE(res.error() == std::errc::operation_canceled);
        });
    }
}

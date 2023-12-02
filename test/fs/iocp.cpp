#include <asyncio/fs/iocp.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

TEST_CASE("IOCP", "[filesystem framework]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        const auto eventLoop = asyncio::getEventLoop();
        REQUIRE(eventLoop);

        auto framework = asyncio::fs::makeIOCP();
        REQUIRE(framework);

        SECTION("normal") {
            const auto path = std::filesystem::temp_directory_path() / "asyncio-fs-iocp";
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

            const auto fd = reinterpret_cast<asyncio::FileDescriptor>(handle);
            const auto result = framework->associate(fd);
            REQUIRE(result);

            constexpr std::string_view content = "hello";
            auto n = co_await framework->write(eventLoop, fd, 0, std::as_bytes(std::span{content}));
            REQUIRE(n);
            REQUIRE(*n == content.size());

            std::byte data[5];
            n = co_await framework->read(eventLoop, fd, 0, data);
            REQUIRE(n);
            REQUIRE(*n == sizeof(data));
            REQUIRE(memcmp(data, content.data(), content.size()) == 0);
            REQUIRE(CloseHandle(handle));
        }

        SECTION("cancel") {
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

            const auto fd = reinterpret_cast<asyncio::FileDescriptor>(pipe);
            const auto result = framework->associate(fd);
            REQUIRE(result);

            std::byte data[5];
            auto task = framework->read(eventLoop, fd, 0, data);

            task.cancel();
            co_await task;
            const auto n = task.result();

            REQUIRE(!n);
            REQUIRE(n.error() == std::errc::operation_canceled);

            REQUIRE(CloseHandle(pipe));
            REQUIRE(CloseHandle(handle));
        }
    });
}

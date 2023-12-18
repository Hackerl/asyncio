#include <asyncio/fs/posix.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <unistd.h>
#include <fcntl.h>

TEST_CASE("posix aio", "[fs]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        const auto eventLoop = asyncio::getEventLoop();
        REQUIRE(eventLoop);

        auto framework = asyncio::fs::makePosixAIO(eventLoop->base());
        REQUIRE(framework);

        const auto path = std::filesystem::temp_directory_path() / "asyncio-fs-posix";
        const int fd = open(path.string().c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        REQUIRE(fd > 0);

        constexpr std::string_view content = "hello";
        auto n = co_await framework->write(eventLoop, fd, 0, std::as_bytes(std::span{content}));
        REQUIRE(n);
        REQUIRE(*n == content.size());

        std::byte data[5];
        n = co_await framework->read(eventLoop, fd, 0, data);
        REQUIRE(n);
        REQUIRE(*n == content.size());
        REQUIRE(memcmp(data, content.data(), content.size()) == 0);

        REQUIRE(close(fd) == 0);
        REQUIRE(std::filesystem::remove(path));
    });
}

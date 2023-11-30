#include <asyncio/fs/posix.h>
#include <asyncio/event_loop.h>
#include <catch2/catch_test_macros.hpp>
#include <zero/defer.h>
#include <filesystem>
#include <unistd.h>
#include <fcntl.h>

TEST_CASE("posix aio", "[filesystem framework]") {
    SECTION("posix aio") {
        asyncio::run([]() -> zero::async::coroutine::Task<void> {
            const auto eventLoop = asyncio::getEventLoop();
            REQUIRE(eventLoop);

            auto framework = asyncio::fs::makePosixAIO(eventLoop->base());
            REQUIRE(framework);

            const auto path = std::filesystem::temp_directory_path() / "asyncio-fs-posix";
            const int fd = open(path.string().c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
            REQUIRE(fd > 0);
            DEFER(close(fd));
            DEFER(std::filesystem::remove(path));

            constexpr std::array content = {
                std::byte{'h'},
                std::byte{'e'},
                std::byte{'l'},
                std::byte{'l'},
                std::byte{'o'}
            };

            auto result = co_await framework->write(eventLoop, fd, 0, content);
            REQUIRE(result);
            REQUIRE(*result == content.size());

            std::byte data[5];
            result = co_await framework->read(eventLoop, fd, 0, data);
            REQUIRE(result);
            REQUIRE(*result == sizeof(data));
            REQUIRE(std::equal(data, data + sizeof(data), content.begin()));
        });
    }
}

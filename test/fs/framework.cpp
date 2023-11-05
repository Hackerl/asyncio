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
            auto framework = asyncio::fs::makePosixAIO();
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
}
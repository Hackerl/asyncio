#include <asyncio/fs.h>
#include <zero/filesystem/file.h>
#include <zero/strings/strings.h>
#include <catch2/catch_test_macros.hpp>

constexpr std::string_view CONTENT = "hello world";

TEST_CASE("asynchronous filesystem", "[fs]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        const auto path = std::filesystem::temp_directory_path() / "asyncio-fs";
        REQUIRE(zero::filesystem::writeString(path, CONTENT));

        SECTION("read only") {
            auto file = co_await asyncio::fs::open(path, UV_FS_O_RDONLY);
            REQUIRE(file);

            std::string content;
            content.resize(CONTENT.size());

            auto n = co_await file->read(std::as_writable_bytes(std::span{content}));
            REQUIRE(n);
            REQUIRE(*n == CONTENT.size());
            REQUIRE(content == CONTENT);

            n = co_await file->read(std::as_writable_bytes(std::span{content}));
            REQUIRE(n);
            REQUIRE(*n == 0);
        }

        SECTION("write only") {
            auto file = co_await asyncio::fs::open(path, UV_FS_O_WRONLY);
            REQUIRE(file);

            const std::string replace = zero::strings::toupper(CONTENT);
            const auto res = co_await file->writeAll(std::as_bytes(std::span{replace}));
            REQUIRE(res);

            const auto content = zero::filesystem::readString(path);
            REQUIRE(content);
            REQUIRE(content == replace);
        }

        SECTION("read and write") {
            auto file = co_await asyncio::fs::open(path, UV_FS_O_RDWR);
            REQUIRE(file);

            std::string content;
            content.resize(CONTENT.size());

            auto n = co_await file->read(std::as_writable_bytes(std::span{content}));
            REQUIRE(n);
            REQUIRE(*n == CONTENT.size());
            REQUIRE(content == CONTENT);

            n = co_await file->read(std::as_writable_bytes(std::span{content}));
            REQUIRE(n);
            REQUIRE(*n == 0);

            const auto res = co_await file->writeAll(std::as_bytes(std::span{CONTENT}));
            REQUIRE(res);

            const auto length = co_await file->length();
            REQUIRE(length);
            REQUIRE(*length == CONTENT.size() * 2);
        }

        SECTION("append") {
            auto file = co_await asyncio::fs::open(path, UV_FS_O_RDWR | UV_FS_O_APPEND);
            REQUIRE(file);

            auto pos = co_await file->position();
            REQUIRE(pos);
            REQUIRE(*pos == 0);

            auto res = co_await file->writeAll(std::as_bytes(std::span{CONTENT}));
            REQUIRE(res);

            pos = co_await file->position();
            REQUIRE(pos);
            REQUIRE(*pos == CONTENT.size() * 2);

            std::string content;
            content.resize(CONTENT.size());

            auto n = co_await file->read(std::as_writable_bytes(std::span{content}));
            REQUIRE(n);
            REQUIRE(*n == 0);

            res = co_await file->rewind();
            REQUIRE(res);

            n = co_await file->read(std::as_writable_bytes(std::span{content}));
            REQUIRE(n);
            REQUIRE(*n == CONTENT.size());
            REQUIRE(content == CONTENT);

            n = co_await file->read(std::as_writable_bytes(std::span{content}));
            REQUIRE(n);
            REQUIRE(*n == CONTENT.size());
            REQUIRE(content == CONTENT);

            n = co_await file->read(std::as_writable_bytes(std::span{content}));
            REQUIRE(n);
            REQUIRE(*n == 0);
        }

        SECTION("create") {
            REQUIRE(std::filesystem::remove(path));

            const auto file = co_await asyncio::fs::open(path, UV_FS_O_RDONLY | UV_FS_O_CREAT);
            REQUIRE(file);
            REQUIRE(std::filesystem::exists(path));
        }

        SECTION("truncate") {
            auto file = co_await asyncio::fs::open(path, UV_FS_O_RDONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC);
            REQUIRE(file);

            const auto length = co_await file->length();
            REQUIRE(length);
            REQUIRE(*length == 0);
        }

        SECTION("create new") {
            SECTION("success") {
                REQUIRE(std::filesystem::remove(path));

                const auto file = co_await asyncio::fs::open(path, UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_EXCL);
                REQUIRE(file);
                REQUIRE(std::filesystem::exists(path));
            }

            SECTION("failure") {
                const auto file = co_await asyncio::fs::open(path, UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_EXCL);
                REQUIRE(!file);
                REQUIRE(file.error() == std::errc::file_exists);
            }
        }

        SECTION("close") {
            auto file = co_await asyncio::fs::open(path, UV_FS_O_RDONLY);
            REQUIRE(file);

            const auto res = co_await file->close();
            REQUIRE(res);
        }

        REQUIRE(std::filesystem::remove(path));
    });
    REQUIRE(result);
    REQUIRE(*result);
}

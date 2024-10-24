#include "async.h"
#include <asyncio/fs.h>
#include <zero/strings/strings.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

constexpr std::string_view CONTENT = "hello world";

ASYNC_TEST_CASE("file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-fs-file";

    REQUIRE(co_await asyncio::fs::write(path, CONTENT));

    SECTION("read only") {
        auto file = co_await asyncio::fs::open(path, O_RDONLY);
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
        auto file = co_await asyncio::fs::open(path, O_WRONLY);
        REQUIRE(file);

        const auto replace = zero::strings::toupper(CONTENT);
        const auto res = co_await file->writeAll(std::as_bytes(std::span{replace}));
        REQUIRE(res);

        const auto content = co_await asyncio::fs::readString(path);
        REQUIRE(content);
        REQUIRE(content == replace);
    }

    SECTION("read and write") {
        auto file = co_await asyncio::fs::open(path, O_RDWR);
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
        auto file = co_await asyncio::fs::open(path, O_RDWR | O_APPEND);
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
        REQUIRE(co_await asyncio::fs::remove(path));

        const auto file = co_await asyncio::fs::open(path, O_RDONLY | O_CREAT);
        REQUIRE(file);
        REQUIRE(co_await asyncio::fs::exists(path));
    }

    SECTION("truncate") {
        auto file = co_await asyncio::fs::open(path, O_RDONLY | O_CREAT | O_TRUNC);
        REQUIRE(file);

        const auto length = co_await file->length();
        REQUIRE(length);
        REQUIRE(*length == 0);
    }

    SECTION("create new") {
        SECTION("success") {
            REQUIRE(co_await asyncio::fs::remove(path));

            const auto file = co_await asyncio::fs::open(path, O_WRONLY | O_CREAT | O_EXCL);
            REQUIRE(file);
            REQUIRE(co_await asyncio::fs::exists(path));
        }

        SECTION("failure") {
            const auto file = co_await asyncio::fs::open(path, O_WRONLY | O_CREAT | O_EXCL);
            REQUIRE_FALSE(file);
            REQUIRE(file.error() == std::errc::file_exists);
        }
    }

    SECTION("close") {
        auto file = co_await asyncio::fs::open(path, O_RDONLY);
        REQUIRE(file);

        const auto res = co_await file->close();
        REQUIRE(res);
    }

    REQUIRE(co_await asyncio::fs::remove(path));
}

ASYNC_TEST_CASE("read bytes from file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-fs-read-bytes";

    REQUIRE_FALSE((co_await asyncio::fs::exists(path)).value_or(false));

    auto content = co_await asyncio::fs::read(path);
    REQUIRE_FALSE(content);
    REQUIRE(content.error() == std::errc::no_such_file_or_directory);

    REQUIRE(co_await asyncio::fs::write(path, std::as_bytes(std::span{CONTENT})));
    REQUIRE((co_await asyncio::fs::exists(path)).value_or(false));

    content = co_await asyncio::fs::read(path);
    REQUIRE(content);
    REQUIRE_THAT(*content, Catch::Matchers::RangeEquals(std::as_bytes(std::span{CONTENT})));

    REQUIRE(co_await asyncio::fs::remove(path));
}

ASYNC_TEST_CASE("read string from file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-fs-read-string";

    REQUIRE_FALSE((co_await asyncio::fs::exists(path)).value_or(false));

    auto content = co_await asyncio::fs::readString(path);
    REQUIRE_FALSE(content);
    REQUIRE(content.error() == std::errc::no_such_file_or_directory);

    REQUIRE(co_await asyncio::fs::write(path, std::as_bytes(std::span{CONTENT})));
    REQUIRE((co_await asyncio::fs::exists(path)).value_or(false));

    content = co_await asyncio::fs::readString(path);
    REQUIRE(content);
    REQUIRE(*content == CONTENT);

    REQUIRE(co_await asyncio::fs::remove(path));
}

ASYNC_TEST_CASE("write bytes to file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-fs-write-bytes";

    REQUIRE_FALSE((co_await asyncio::fs::exists(path)).value_or(false));

    REQUIRE(co_await asyncio::fs::write(path, std::as_bytes(std::span{CONTENT})));
    REQUIRE((co_await asyncio::fs::exists(path)).value_or(false));

    const auto content = co_await asyncio::fs::read(path);
    REQUIRE(content);
    REQUIRE_THAT(*content, Catch::Matchers::RangeEquals(std::as_bytes(std::span{CONTENT})));

    REQUIRE(co_await asyncio::fs::remove(path));
}

ASYNC_TEST_CASE("write string to file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-fs-write-string";

    REQUIRE_FALSE((co_await asyncio::fs::exists(path)).value_or(false));

    REQUIRE(co_await asyncio::fs::write(path, CONTENT));
    REQUIRE((co_await asyncio::fs::exists(path)).value_or(false));

    const auto content = co_await asyncio::fs::readString(path);
    REQUIRE(content);
    REQUIRE(*content == CONTENT);

    REQUIRE(co_await asyncio::fs::remove(path));
}

ASYNC_TEST_CASE("read directory", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto directory = *temp / "asyncio-fs-read-directory";

    auto it = co_await asyncio::fs::readDirectory(directory / "z");
    REQUIRE_FALSE(it);
    REQUIRE(it.error() == std::errc::no_such_file_or_directory);

    REQUIRE(co_await asyncio::fs::createDirectory(directory));

    const std::list files{directory / "a", directory / "b", directory / "c"};

    for (const auto &file: files) {
        REQUIRE(co_await asyncio::fs::write(file, ""));
    }

    it = co_await asyncio::fs::readDirectory(directory);
    REQUIRE(it);

    auto entry = co_await it->next();
    REQUIRE(entry);
    REQUIRE(*entry);
    REQUIRE_THAT(files, Catch::Matchers::Contains(entry.value()->path()));

    entry = co_await it->next();
    REQUIRE(entry);
    REQUIRE(*entry);
    REQUIRE_THAT(files, Catch::Matchers::Contains(entry.value()->path()));

    entry = co_await it->next();
    REQUIRE(entry);
    REQUIRE(*entry);
    REQUIRE_THAT(files, Catch::Matchers::Contains(entry.value()->path()));

    entry = co_await it->next();
    REQUIRE(entry);
    REQUIRE_FALSE(*entry);

    REQUIRE(co_await asyncio::fs::removeAll(directory));
}

ASYNC_TEST_CASE("walk directory", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto directory = *temp / "asyncio-fs-walk-directory";

    auto it = co_await asyncio::fs::walkDirectory(directory / "z");
    REQUIRE_FALSE(it);
    REQUIRE(it.error() == std::errc::no_such_file_or_directory);

    REQUIRE(co_await asyncio::fs::createDirectory(directory));

    const std::list files{directory / "a", directory / "b" / "c", directory / "d" / "e" / "f"};

    for (const auto &file: files) {
        REQUIRE(co_await asyncio::fs::createDirectories(file.parent_path()));
        REQUIRE(co_await asyncio::fs::write(file, ""));
    }

    it = co_await asyncio::fs::walkDirectory(directory);
    REQUIRE(it);

    std::list<std::filesystem::path> paths;

    while (true) {
        const auto entry = co_await it->next();
        REQUIRE(entry);

        if (!*entry)
            break;

        if (!(co_await entry.value()->isRegularFile()).value_or(false))
            continue;

        paths.push_back(entry->value().path());
    }

    REQUIRE_THAT(paths, Catch::Matchers::UnorderedRangeEquals(files));
    REQUIRE(co_await asyncio::fs::removeAll(directory));
}

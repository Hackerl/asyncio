#include "catch_extensions.h"
#include <asyncio/fs.h>
#include <zero/strings/strings.h>
#include <catch2/matchers/catch_matchers_all.hpp>

constexpr std::string_view CONTENT = "hello world";

ASYNC_TEST_CASE("file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-fs-file";
    REQUIRE(co_await asyncio::fs::write(path, CONTENT));

    auto file = co_await asyncio::fs::open(path, O_RDWR);
    REQUIRE(file);

    SECTION("fd") {
        const auto fd = file->fd();
#ifdef _WIN32
        REQUIRE(fd != nullptr);
#else
        REQUIRE(fd >= 0);
#endif
    }

    SECTION("read") {
        std::string content;
        content.resize(CONTENT.size());

        REQUIRE(co_await file->read(std::as_writable_bytes(std::span{content})) == CONTENT.size());
        REQUIRE(content == CONTENT);
        REQUIRE(co_await file->read(std::as_writable_bytes(std::span{content})) == 0);
    }

    SECTION("write") {
        const auto newContent = zero::strings::toupper(CONTENT);
        REQUIRE(co_await file->write(std::as_bytes(std::span{newContent})) == newContent.size());
        REQUIRE(co_await asyncio::fs::readString(path) == newContent);
    }

    SECTION("close") {
        REQUIRE(co_await file->close());
    }

    REQUIRE(co_await asyncio::fs::remove(path));
}

ASYNC_TEST_CASE("open file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-fs-open-file";

    SECTION("read only") {
        REQUIRE(co_await asyncio::fs::write(path, CONTENT));

        auto file = co_await asyncio::fs::open(path, O_RDONLY);
        REQUIRE(file);

        const auto content = co_await file->readAll();
        REQUIRE(content);
        REQUIRE_THAT(*content, Catch::Matchers::RangeEquals(std::as_bytes(std::span{CONTENT})));

        REQUIRE_FALSE(co_await file->writeAll(std::as_bytes(std::span{CONTENT})));
        REQUIRE(co_await asyncio::fs::remove(path));
    }

    SECTION("write only") {
        REQUIRE(co_await asyncio::fs::write(path, ""));

        auto file = co_await asyncio::fs::open(path, O_WRONLY);
        REQUIRE(file);

        REQUIRE(co_await file->writeAll(std::as_bytes(std::span{CONTENT})));
        REQUIRE(co_await asyncio::fs::readString(path) == CONTENT);

        REQUIRE_FALSE(co_await file->readAll());
        REQUIRE(co_await asyncio::fs::remove(path));
    }

    SECTION("read and write") {
        REQUIRE(co_await asyncio::fs::write(path, ""));

        auto file = co_await asyncio::fs::open(path, O_RDWR);
        REQUIRE(file);

        REQUIRE(co_await file->writeAll(std::as_bytes(std::span{CONTENT})));
        REQUIRE(co_await file->rewind());

        const auto content = co_await file->readAll();
        REQUIRE(content);
        REQUIRE_THAT(*content, Catch::Matchers::RangeEquals(std::as_bytes(std::span{CONTENT})));

        REQUIRE(co_await asyncio::fs::remove(path));
    }

    SECTION("append") {
        REQUIRE(co_await asyncio::fs::write(path, CONTENT));

        auto file = co_await asyncio::fs::open(path, O_WRONLY | O_APPEND);
        REQUIRE(file);

        REQUIRE(co_await file->position() == 0);
        REQUIRE(co_await file->writeAll(std::as_bytes(std::span{CONTENT})));
        REQUIRE(co_await file->position() == CONTENT.size() * 2);

        const auto content = co_await asyncio::fs::readString(path);
        REQUIRE(content);
        REQUIRE(content->substr(0, CONTENT.size()) == CONTENT);
        REQUIRE(content->substr(CONTENT.size()) == CONTENT);

        REQUIRE(co_await asyncio::fs::remove(path));
    }

    SECTION("create") {
        REQUIRE(co_await asyncio::fs::open(path, O_RDONLY | O_CREAT));
        REQUIRE((co_await asyncio::fs::exists(path)).value_or(false));
        REQUIRE(co_await asyncio::fs::remove(path));
    }

    SECTION("truncate") {
        REQUIRE(co_await asyncio::fs::write(path, CONTENT));

        auto file = co_await asyncio::fs::open(path, O_RDONLY | O_CREAT | O_TRUNC);
        REQUIRE(file);

        const auto content = co_await file->readAll();
        REQUIRE(content);
        REQUIRE_THAT(*content, Catch::Matchers::IsEmpty());

        REQUIRE(co_await asyncio::fs::remove(path));
    }

    SECTION("create new") {
        SECTION("success") {
            REQUIRE(co_await asyncio::fs::open(path, O_WRONLY | O_CREAT | O_EXCL));
            REQUIRE((co_await asyncio::fs::exists(path)).value_or(false));
            REQUIRE(co_await asyncio::fs::remove(path));
        }

        SECTION("failure") {
            REQUIRE(co_await asyncio::fs::write(path, CONTENT));
            REQUIRE_ERROR(co_await asyncio::fs::open(path, O_WRONLY | O_CREAT | O_EXCL), std::errc::file_exists);
            REQUIRE(co_await asyncio::fs::remove(path));
        }
    }
}

ASYNC_TEST_CASE("seekable file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-fs-seekable-file";
    REQUIRE(co_await asyncio::fs::write(path, CONTENT));

    auto file = co_await asyncio::fs::open(path, O_RDONLY);
    REQUIRE(file);

    SECTION("position") {
        REQUIRE(co_await file->position() == 0);
        REQUIRE(co_await file->readAll());
        REQUIRE(co_await file->position() == CONTENT.size());
    }

    SECTION("length") {
        REQUIRE(co_await file->length() == CONTENT.size());
    }

    SECTION("rewind") {
        REQUIRE(co_await file->readAll());
        REQUIRE(co_await file->position() == CONTENT.size());
        REQUIRE(co_await file->rewind());
        REQUIRE(co_await file->position() == 0);
    }

    SECTION("seek") {
        static_assert(CONTENT.size() > 6);

        SECTION("begin") {
            REQUIRE(co_await file->seek(6, asyncio::ISeekable::Whence::BEGIN));
        }

        SECTION("current") {
            REQUIRE(co_await file->seek(6, asyncio::ISeekable::Whence::CURRENT));
        }

        SECTION("end") {
            REQUIRE(co_await file->seek(
                -(static_cast<std::int64_t>(CONTENT.size() - 6)),
                asyncio::ISeekable::Whence::END
            ));
        }

        const auto content = co_await file->readAll();
        REQUIRE(content);
        REQUIRE_THAT(*content, Catch::Matchers::SizeIs(CONTENT.size() - 6));
        REQUIRE_THAT(
            std::string{CONTENT},
            Catch::Matchers::EndsWith(std::string{reinterpret_cast<const char *>(content->data()), content->size()})
        );
    }

    REQUIRE(co_await asyncio::fs::remove(path));
}

ASYNC_TEST_CASE("read bytes from file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-fs-read-bytes";

    SECTION("file does not exist") {
        REQUIRE_ERROR(co_await asyncio::fs::read(path), std::errc::no_such_file_or_directory);
    }

    SECTION("file exists") {
        REQUIRE(co_await asyncio::fs::write(path, std::as_bytes(std::span{CONTENT})));

        const auto content = co_await asyncio::fs::read(path);
        REQUIRE(content);
        REQUIRE_THAT(*content, Catch::Matchers::RangeEquals(std::as_bytes(std::span{CONTENT})));

        REQUIRE(co_await asyncio::fs::remove(path));
    }
}

ASYNC_TEST_CASE("read string from file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-fs-read-string";

    SECTION("file does not exist") {
        REQUIRE_ERROR(co_await asyncio::fs::readString(path), std::errc::no_such_file_or_directory);
    }

    SECTION("file exists") {
        REQUIRE(co_await asyncio::fs::write(path, std::as_bytes(std::span{CONTENT})));
        REQUIRE(co_await asyncio::fs::readString(path) == CONTENT);
        REQUIRE(co_await asyncio::fs::remove(path));
    }
}

ASYNC_TEST_CASE("write bytes to file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-fs-write-bytes";

    REQUIRE(co_await asyncio::fs::write(path, std::as_bytes(std::span{CONTENT})));

    const auto content = co_await asyncio::fs::read(path);
    REQUIRE(content);
    REQUIRE_THAT(*content, Catch::Matchers::RangeEquals(std::as_bytes(std::span{CONTENT})));

    REQUIRE(co_await asyncio::fs::remove(path));
}

ASYNC_TEST_CASE("write string to file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-fs-write-string";

    REQUIRE(co_await asyncio::fs::write(path, CONTENT));
    REQUIRE(co_await asyncio::fs::readString(path) == CONTENT);
    REQUIRE(co_await asyncio::fs::remove(path));
}

ASYNC_TEST_CASE("read directory", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto directory = *temp / "asyncio-fs-read-directory";

    SECTION("directory not exists") {
        REQUIRE_ERROR(co_await asyncio::fs::readDirectory(directory / "z"), std::errc::no_such_file_or_directory);
    }

    SECTION("directory exists") {
        REQUIRE(co_await asyncio::fs::createDirectory(directory));

        const std::list files{directory / "a", directory / "b", directory / "c"};

        for (const auto &file: files) {
            REQUIRE(co_await asyncio::fs::write(file, ""));
        }

        auto it = co_await asyncio::fs::readDirectory(directory);
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
}

ASYNC_TEST_CASE("walk directory", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto directory = *temp / "asyncio-fs-walk-directory";

    SECTION("directory not exists") {
        REQUIRE_ERROR(co_await asyncio::fs::walkDirectory(directory / "z"), std::errc::no_such_file_or_directory);
    }

    SECTION("directory exists") {
        REQUIRE(co_await asyncio::fs::createDirectory(directory));

        const std::list files{directory / "a", directory / "b" / "c", directory / "d" / "e" / "f"};

        for (const auto &file: files) {
            REQUIRE(co_await asyncio::fs::createDirectories(file.parent_path()));
            REQUIRE(co_await asyncio::fs::write(file, ""));
        }

        auto it = co_await asyncio::fs::walkDirectory(directory);
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
}

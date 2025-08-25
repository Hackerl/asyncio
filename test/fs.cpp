#include "catch_extensions.h"
#include <asyncio/fs.h>
#include <zero/strings/strings.h>
#include <catch2/matchers/catch_matchers_all.hpp>

ASYNC_TEST_CASE("file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / GENERATE(take(1, randomAlphanumericString(8, 64)));
    const auto content = GENERATE(take(1, randomBytes(1, 102400)));

    auto file = co_await asyncio::fs::open(path, O_RDWR | O_CREAT);
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
        REQUIRE(co_await asyncio::fs::write(path, content));

        std::vector<std::byte> data;
        data.resize(content.size());

        REQUIRE(co_await file->read(data) == content.size());
        REQUIRE(data == content);
        REQUIRE(co_await file->read(data) == 0);
    }

    SECTION("write") {
        REQUIRE(co_await file->write(content) == content.size());
        REQUIRE(co_await asyncio::fs::read(path) == content);
    }

    SECTION("close") {
        REQUIRE(co_await file->close());
    }

    REQUIRE(co_await asyncio::fs::remove(path));
}

ASYNC_TEST_CASE("open file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / GENERATE(take(1, randomAlphanumericString(8, 64)));
    const auto input = GENERATE(take(1, randomBytes(1, 102400)));

    SECTION("read only") {
        REQUIRE(co_await asyncio::fs::write(path, input));

        auto file = co_await asyncio::fs::open(path, O_RDONLY);
        REQUIRE(file);

        REQUIRE(co_await file->readAll() == input);

        REQUIRE_FALSE(co_await file->writeAll(input));
        REQUIRE(co_await asyncio::fs::remove(path));
    }

    SECTION("write only") {
        REQUIRE(co_await asyncio::fs::write(path, ""));

        auto file = co_await asyncio::fs::open(path, O_WRONLY);
        REQUIRE(file);

        REQUIRE(co_await file->writeAll(input));
        REQUIRE(co_await asyncio::fs::read(path) == input);

        REQUIRE_FALSE(co_await file->readAll());
        REQUIRE(co_await asyncio::fs::remove(path));
    }

    SECTION("read and write") {
        REQUIRE(co_await asyncio::fs::write(path, ""));

        auto file = co_await asyncio::fs::open(path, O_RDWR);
        REQUIRE(file);

        REQUIRE(co_await file->writeAll(input));
        REQUIRE(co_await file->rewind());
        REQUIRE(co_await file->readAll() == input);

        REQUIRE(co_await asyncio::fs::remove(path));
    }

    SECTION("append") {
        REQUIRE(co_await asyncio::fs::write(path, input));

        auto file = co_await asyncio::fs::open(path, O_WRONLY | O_APPEND);
        REQUIRE(file);

        REQUIRE(co_await file->position() == 0);
        REQUIRE(co_await file->writeAll(input));
        REQUIRE(co_await file->position() == input.size() * 2);

        const auto content = co_await asyncio::fs::read(path);
        REQUIRE(content);
        REQUIRE_THAT(
            (std::span{content->begin(), content->begin() + input.size()}),
            Catch::Matchers::RangeEquals(input)
        );

        REQUIRE_THAT(
            (std::span{content->begin() + input.size(), content->end()}),
            Catch::Matchers::RangeEquals(input)
        );

        REQUIRE(co_await asyncio::fs::remove(path));
    }

    SECTION("create") {
        REQUIRE(co_await asyncio::fs::open(path, O_RDONLY | O_CREAT));
        REQUIRE((co_await asyncio::fs::exists(path)).value_or(false));
        REQUIRE(co_await asyncio::fs::remove(path));
    }

    SECTION("truncate") {
        REQUIRE(co_await asyncio::fs::write(path, input));

        auto file = co_await asyncio::fs::open(path, O_RDONLY | O_CREAT | O_TRUNC);
        REQUIRE(file);

        const auto data = co_await file->readAll();
        REQUIRE(data);
        REQUIRE_THAT(*data, Catch::Matchers::IsEmpty());

        REQUIRE(co_await asyncio::fs::remove(path));
    }

    SECTION("create new") {
        SECTION("success") {
            REQUIRE(co_await asyncio::fs::open(path, O_WRONLY | O_CREAT | O_EXCL));
            REQUIRE((co_await asyncio::fs::exists(path)).value_or(false));
            REQUIRE(co_await asyncio::fs::remove(path));
        }

        SECTION("failure") {
            REQUIRE(co_await asyncio::fs::write(path, input));
            REQUIRE_ERROR(co_await asyncio::fs::open(path, O_WRONLY | O_CREAT | O_EXCL), std::errc::file_exists);
            REQUIRE(co_await asyncio::fs::remove(path));
        }
    }
}

ASYNC_TEST_CASE("seekable file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / GENERATE(take(1, randomAlphanumericString(8, 64)));
    const auto content = GENERATE(take(1, randomBytes(1, 102400)));

    REQUIRE(co_await asyncio::fs::write(path, content));

    auto file = co_await asyncio::fs::open(path, O_RDONLY);
    REQUIRE(file);

    SECTION("position") {
        REQUIRE(co_await file->position() == 0);
        REQUIRE(co_await file->readAll());
        REQUIRE(co_await file->position() == content.size());
    }

    SECTION("length") {
        REQUIRE(co_await file->length() == content.size());
    }

    SECTION("rewind") {
        REQUIRE(co_await file->readAll());
        REQUIRE(co_await file->position() == content.size());
        REQUIRE(co_await file->rewind());
        REQUIRE(co_await file->position() == 0);
    }

    SECTION("seek") {
        const auto offset = GENERATE_REF(take(1, random<std::size_t>(0, content.size() - 1)));

        SECTION("begin") {
            REQUIRE(co_await file->seek(offset, asyncio::ISeekable::Whence::BEGIN) == offset);
        }

        SECTION("current") {
            REQUIRE(co_await file->seek(offset, asyncio::ISeekable::Whence::CURRENT) == offset);
        }

        SECTION("end") {
            REQUIRE(co_await file->seek(
                -(static_cast<std::int64_t>(content.size() - offset)),
                asyncio::ISeekable::Whence::END
            ) == offset);
        }

        const auto data = co_await file->readAll();
        REQUIRE(data);
        REQUIRE_THAT(*data, Catch::Matchers::RangeEquals(std::span{content.begin() + offset, content.end()}));
    }

    REQUIRE(co_await asyncio::fs::remove(path));
}

ASYNC_TEST_CASE("read bytes from file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / GENERATE(take(1, randomAlphanumericString(8, 64)));

    SECTION("file does not exist") {
        REQUIRE_ERROR(co_await asyncio::fs::read(path), std::errc::no_such_file_or_directory);
    }

    SECTION("file exists") {
        const auto content = GENERATE(take(1, randomBytes(1, 102400)));
        REQUIRE(co_await asyncio::fs::write(path, content));
        REQUIRE(co_await asyncio::fs::read(path) == content);
        REQUIRE(co_await asyncio::fs::remove(path));
    }
}

ASYNC_TEST_CASE("read string from file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / GENERATE(take(1, randomAlphanumericString(8, 64)));

    SECTION("file does not exist") {
        REQUIRE_ERROR(co_await asyncio::fs::readString(path), std::errc::no_such_file_or_directory);
    }

    SECTION("file exists") {
        const auto content = GENERATE(take(1, randomString(1, 102400)));
        REQUIRE(co_await asyncio::fs::write(path, content));
        REQUIRE(co_await asyncio::fs::readString(path) == content);
        REQUIRE(co_await asyncio::fs::remove(path));
    }
}

ASYNC_TEST_CASE("write bytes to file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / GENERATE(take(1, randomAlphanumericString(8, 64)));
    const auto content = GENERATE(take(1, randomBytes(1, 102400)));

    REQUIRE(co_await asyncio::fs::write(path, content));
    REQUIRE(co_await asyncio::fs::read(path) == content);
    REQUIRE(co_await asyncio::fs::remove(path));
}

ASYNC_TEST_CASE("write string to file", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / GENERATE(take(1, randomAlphanumericString(8, 64)));
    const auto content = GENERATE(take(1, randomString(1, 102400)));

    REQUIRE(co_await asyncio::fs::write(path, content));
    REQUIRE(co_await asyncio::fs::readString(path) == content);
    REQUIRE(co_await asyncio::fs::remove(path));
}

ASYNC_TEST_CASE("read directory", "[fs]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto directory = *temp / GENERATE(take(1, randomAlphanumericString(8, 64)));

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

    const auto directory = *temp / GENERATE(take(1, randomAlphanumericString(8, 64)));

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

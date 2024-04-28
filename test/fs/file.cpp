#include <asyncio/fs/file.h>
#include <zero/filesystem/file.h>
#include <zero/strings/strings.h>
#include <catch2/catch_test_macros.hpp>
#include <fcntl.h>

constexpr std::string_view CONTENT = "hello world";
const auto path = std::filesystem::temp_directory_path() / "asyncio-fs";

TEST_CASE("asynchronous filesystem", "[fs]") {
    REQUIRE(zero::filesystem::writeString(path, CONTENT));

    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("read only") {
            auto file = asyncio::fs::open(path);
            REQUIRE(file);

            SECTION("read") {
                std::byte data[6];
                auto n = co_await file->read(data);
                REQUIRE(n);
                REQUIRE(*n == 6);
                REQUIRE(memcmp(data, "hello ", 6) == 0);

                n = co_await file->read(data);
                REQUIRE(n);
                REQUIRE(*n == 5);
                REQUIRE(memcmp(data, "world", 5) == 0);

                n = co_await file->read(data);
                REQUIRE(n);
                REQUIRE(*n == 0);
            }

            SECTION("read all") {
                const auto result = co_await file->readAll();
                REQUIRE(result);
                REQUIRE(result->size() == CONTENT.size());
                REQUIRE(memcmp(result->data(), CONTENT.data(), CONTENT.size()) == 0);

                std::byte data[1024];
                const auto n = co_await file->read(data);
                REQUIRE(n);
                REQUIRE(*n == 0);
            }

            SECTION("write") {
                const auto result = co_await file->write(std::as_bytes(std::span{CONTENT}));
                REQUIRE(!result);
            }
        }

        SECTION("write only") {
            auto file = asyncio::fs::open(path, O_WRONLY);
            REQUIRE(file);

            SECTION("read") {
                std::byte data[6];
                const auto result = co_await file->read(data);
                REQUIRE(!result);
            }

            SECTION("write") {
                const std::string replace = zero::strings::toupper(CONTENT);
                const auto n = co_await file->write(std::as_bytes(std::span{replace}));
                REQUIRE(n);
                REQUIRE(*n == replace.size());

                const auto content = zero::filesystem::readString(path);
                REQUIRE(content);
                REQUIRE(content == replace);
            }
        }

        SECTION("read and write") {
            auto file = asyncio::fs::open(path, O_RDWR);
            REQUIRE(file);

            std::byte data[6];
            auto n = co_await file->read(data);
            REQUIRE(n);
            REQUIRE(*n == 6);
            REQUIRE(memcmp(data, "hello ", 6) == 0);

            n = co_await file->write(std::as_bytes(std::span{CONTENT}));
            REQUIRE(n);
            REQUIRE(*n == CONTENT.size());

            const auto content = zero::filesystem::readString(path);
            REQUIRE(content);
            REQUIRE(content == "hello hello world");
        }

        SECTION("seek") {
            auto file = asyncio::fs::open(path);
            REQUIRE(file);

            std::byte data[5];
            auto n = co_await file->read(data);
            REQUIRE(n);
            REQUIRE(*n == 5);
            REQUIRE(memcmp(data, "hello", 5) == 0);

            SECTION("begin") {
                SECTION("normal") {
                    const auto pos = file->seek(2, asyncio::ISeekable::Whence::BEGIN);
                    REQUIRE(pos);
                    REQUIRE(*pos == 2);

                    n = co_await file->read(data);
                    REQUIRE(n);
                    REQUIRE(*n == 5);
                    REQUIRE(memcmp(data, "llo w", 5) == 0);
                }

                SECTION("invalid") {
                    const auto pos = file->seek(-1, asyncio::ISeekable::Whence::BEGIN);
                    REQUIRE(!pos);
                    REQUIRE(pos.error() == std::errc::invalid_argument);
                }
            }

            SECTION("current") {
                SECTION("normal") {
                    const auto pos = file->seek(1, asyncio::ISeekable::Whence::CURRENT);
                    REQUIRE(pos);
                    REQUIRE(*pos == 6);

                    n = co_await file->read(data);
                    REQUIRE(n);
                    REQUIRE(*n == 5);
                    REQUIRE(memcmp(data, "world", 5) == 0);
                }

                SECTION("invalid") {
                    const auto pos = file->seek(-6, asyncio::ISeekable::Whence::CURRENT);
                    REQUIRE(!pos);
                    REQUIRE(pos.error() == std::errc::invalid_argument);
                }
            }

            SECTION("end") {
                SECTION("normal") {
                    const auto pos = file->seek(-1, asyncio::ISeekable::Whence::END);
                    REQUIRE(pos);
                    REQUIRE(*pos == 10);

                    n = co_await file->read(data);
                    REQUIRE(n);
                    REQUIRE(*n == 1);
                    REQUIRE(data[0] == std::byte{'d'});
                }

                SECTION("invalid") {
                    const auto pos = file->seek(-12, asyncio::ISeekable::Whence::END);
                    REQUIRE(!pos);
                    REQUIRE(pos.error() == std::errc::invalid_argument);
                }
            }
        }

        SECTION("rewind") {
            auto file = asyncio::fs::open(path);
            REQUIRE(file);

            std::byte data[5];
            auto n = co_await file->read(data);
            REQUIRE(n);
            REQUIRE(*n == 5);
            REQUIRE(memcmp(data, "hello", 5) == 0);

            REQUIRE(file->rewind());
            const auto pos = file->position();
            REQUIRE(pos);
            REQUIRE(*pos == 0);

            n = co_await file->read(data);
            REQUIRE(n);
            REQUIRE(*n == 5);
            REQUIRE(memcmp(data, "hello", 5) == 0);
        }

        SECTION("length") {
            auto file = asyncio::fs::open(path, O_RDWR | O_APPEND);
            REQUIRE(file);

            auto length = file->length();
            REQUIRE(length);
            REQUIRE(*length == CONTENT.size());

            const auto n = co_await file->write(std::as_bytes(std::span{CONTENT}));
            REQUIRE(n);
            REQUIRE(*n == CONTENT.size());

            length = file->length();
            REQUIRE(length);
            REQUIRE(*length == CONTENT.size() * 2);
        }

        SECTION("append") {
            auto file = asyncio::fs::open(path, O_RDWR | O_APPEND);
            REQUIRE(file);

            auto pos = file->position();
            REQUIRE(pos);
            REQUIRE(*pos == 0);

            std::byte data[5];
            auto n = co_await file->read(data);
            REQUIRE(n);
            REQUIRE(*n == 5);
            REQUIRE(memcmp(data, "hello", 5) == 0);

            pos = file->position();
            REQUIRE(pos);
            REQUIRE(*pos == 5);

            n = co_await file->write(std::as_bytes(std::span{CONTENT}));
            REQUIRE(n);
            REQUIRE(*n == CONTENT.size());

            pos = file->position();
            REQUIRE(pos);
            REQUIRE(*pos == CONTENT.size() * 2);

            n = co_await file->read(data);
            REQUIRE(n);
            REQUIRE(*n == 0);
            REQUIRE(file->rewind());

            n = co_await file->read(data);
            REQUIRE(n);
            REQUIRE(*n == 5);
            REQUIRE(memcmp(data, "hello", 5) == 0);
        }

        SECTION("create") {
            REQUIRE(std::filesystem::remove(path));

            auto file = asyncio::fs::open(path, O_WRONLY | O_CREAT);
            REQUIRE(file);
            REQUIRE(std::filesystem::exists(path));

            auto length = file->length();
            REQUIRE(length);
            REQUIRE(*length == 0);

            const auto n = co_await file->write(std::as_bytes(std::span{CONTENT}));
            REQUIRE(n);
            REQUIRE(*n == CONTENT.size());

            length = file->length();
            REQUIRE(length);
            REQUIRE(*length == CONTENT.size());
        }

        SECTION("truncate") {
            auto file = asyncio::fs::open(path, O_WRONLY | O_TRUNC);
            REQUIRE(file);

            auto length = file->length();
            REQUIRE(length);
            REQUIRE(*length == 0);

            const auto n = co_await file->write(std::as_bytes(std::span{CONTENT}));
            REQUIRE(n);
            REQUIRE(*n == CONTENT.size());

            length = file->length();
            REQUIRE(length);
            REQUIRE(*length == CONTENT.size());
        }

        SECTION("create new") {
            SECTION("success") {
                REQUIRE(std::filesystem::remove(path));

                auto file = asyncio::fs::open(path, O_WRONLY | O_CREAT | O_EXCL);
                REQUIRE(file);
                REQUIRE(std::filesystem::exists(path));

                auto length = file->length();
                REQUIRE(length);
                REQUIRE(*length == 0);

                const auto n = co_await file->write(std::as_bytes(std::span{CONTENT}));
                REQUIRE(n);
                REQUIRE(*n == CONTENT.size());

                length = file->length();
                REQUIRE(length);
                REQUIRE(*length == CONTENT.size());
            }

            SECTION("failure") {
                const auto file = asyncio::fs::open(path, O_WRONLY | O_CREAT | O_EXCL);
                REQUIRE(!file);
                REQUIRE(file.error() == std::errc::file_exists);
            }
        }

        SECTION("close") {
            auto file = asyncio::fs::open(path);
            REQUIRE(file);

            std::byte data[5];
            auto n = co_await file->read(data);
            REQUIRE(n);
            REQUIRE(*n == 5);
            REQUIRE(memcmp(data, "hello", 5) == 0);

            const auto result = co_await file->close();
            REQUIRE(result);

            n = co_await file->read(data);
            REQUIRE(!n);
            REQUIRE(n.error() == std::errc::bad_file_descriptor);
        }

        SECTION("from file descriptor") {
#ifdef _WIN32
            const auto handle = CreateFileA(
                path.string().c_str(),
                GENERIC_READ,
                FILE_SHARE_READ,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                nullptr
            );
            REQUIRE(handle != INVALID_HANDLE_VALUE);
            const auto fd = reinterpret_cast<asyncio::FileDescriptor>(handle);
#else
            const int fd = open(path.string().c_str(), O_RDONLY);
            REQUIRE(fd != -1);
#endif

            auto file = asyncio::fs::File::from(fd);

            std::byte data[6];
            auto n = co_await file->read(data);
            REQUIRE(n);
            REQUIRE(*n == 6);
            REQUIRE(memcmp(data, "hello ", 6) == 0);

            n = co_await file->read(data);
            REQUIRE(n);
            REQUIRE(*n == 5);
            REQUIRE(memcmp(data, "world", 5) == 0);

            n = co_await file->read(data);
            REQUIRE(n);
            REQUIRE(*n == 0);
        }
    });

    REQUIRE(std::filesystem::remove(path));
}

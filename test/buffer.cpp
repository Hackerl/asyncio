#include "catch_extensions.h"
#include <asyncio/buffer.h>
#include <catch2/matchers/catch_matchers_all.hpp>

ASYNC_TEST_CASE("buffer reader", "[buffer]") {
    const auto input = GENERATE(take(10, randomBytes(1, 10240)));
    const auto capacity = GENERATE(1uz, take(1, random(2uz, 10240uz)));

    SECTION("capacity") {
        asyncio::BufReader reader{asyncio::BytesReader{input}, capacity};
        REQUIRE(reader.capacity() == capacity);
    }

    SECTION("available") {
        asyncio::BufReader reader{asyncio::BytesReader{input}, capacity};

        SECTION("empty") {
            REQUIRE(reader.available() == 0);
        }

        SECTION("not empty") {
            std::vector<std::byte> data;
            REQUIRE(co_await reader.read(data) == 0);
            REQUIRE(reader.available() == (std::min)(input.size(), capacity));
        }
    }

    SECTION("read") {
        asyncio::BufReader reader{asyncio::BytesReader{input}, capacity};

        SECTION("normal") {
            const auto size = GENERATE_REF(take(1, random(1uz, input.size() * 2)));

            std::vector<std::byte> data;
            data.resize(size);

            const auto n = co_await reader.read(data);
            REQUIRE(n == (std::min)(size, input.size()));

            data.resize(*n);
            REQUIRE_THAT(data, Catch::Matchers::RangeEquals(std::span{input.data(), *n}));
        }

        SECTION("eof") {
            REQUIRE(co_await reader.readAll());
            std::array<std::byte, 64> data{};
            REQUIRE(co_await reader.read(data) == 0);
        }
    }

    SECTION("peek") {
        asyncio::BufReader reader{asyncio::BytesReader{input}, capacity};

        SECTION("normal") {
            const auto limit = (std::min)(input.size(), capacity);
            const auto size = GENERATE_REF(take(1, random(1uz, limit)));

            std::vector<std::byte> data;
            data.resize(size);

            REQUIRE(co_await reader.peek(data));
            REQUIRE_THAT(data, Catch::Matchers::RangeEquals(std::span{input.data(), size}));
            REQUIRE(reader.available() == limit);
        }

        SECTION("invalid argument") {
            const auto size = GENERATE_REF(take(1, random(capacity + 1, capacity * 2)));

            std::vector<std::byte> data;
            data.resize(size);
            REQUIRE_ERROR(co_await reader.peek(data), std::errc::invalid_argument);
        }
    }

    auto inputString = GENERATE(take(10, randomAlphanumericString(1, 10240)));

    SECTION("read line") {
        SECTION("normal") {
            const auto pos = GENERATE_REF(take(1, random(0uz, inputString.size() - 1)));

            SECTION("CRLF") {
                inputString.insert(inputString.begin() + static_cast<std::ptrdiff_t>(pos), '\r');
                inputString.insert(inputString.begin() + static_cast<std::ptrdiff_t>(pos) + 1, '\n');
            }

            SECTION("LF") {
                inputString.insert(inputString.begin() + static_cast<std::ptrdiff_t>(pos), '\n');
            }

            asyncio::BufReader reader{asyncio::StringReader{inputString}, capacity};
            REQUIRE(co_await reader.readLine() == inputString.substr(0, pos));
        }

        SECTION("unexpected eof") {
            asyncio::BufReader reader{asyncio::StringReader{inputString}, capacity};
            REQUIRE_ERROR(co_await reader.readLine(), asyncio::IOError::UNEXPECTED_EOF);
        }
    }

    SECTION("read until") {
        const auto c = GENERATE('\t', '\n', '\r', '\x0b', '\x0c');

        SECTION("normal") {
            const auto pos = GENERATE_REF(take(1, random(0uz, inputString.size() - 1)));

            inputString.insert(inputString.begin() + static_cast<std::ptrdiff_t>(pos), c);
            asyncio::BufReader reader{asyncio::StringReader{inputString}, capacity};

            const auto data = co_await reader.readUntil(static_cast<std::byte>(c));
            REQUIRE(data);
            REQUIRE_THAT(*data, Catch::Matchers::RangeEquals(std::as_bytes(std::span{inputString.data(), pos})));
        }

        SECTION("unexpected eof") {
            asyncio::BufReader reader{asyncio::StringReader{inputString}, capacity};
            REQUIRE_ERROR(co_await reader.readUntil(static_cast<std::byte>(c)), asyncio::IOError::UNEXPECTED_EOF);
        }
    }
}

ASYNC_TEST_CASE("buffer writer", "[buffer]") {
    const auto input = GENERATE(take(10, randomBytes(1, 10240)));
    const auto capacity = GENERATE(1uz, take(1, random(2uz, 10240uz)));

    const auto bytesWriter = std::make_shared<asyncio::BytesWriter>();
    asyncio::BufWriter writer{bytesWriter, capacity};

    SECTION("capacity") {
        REQUIRE(writer.capacity() == capacity);
    }

    SECTION("pending") {
        SECTION("empty") {
            REQUIRE(writer.pending() == 0);
        }

        SECTION("not empty") {
            REQUIRE(co_await writer.writeAll(input));
            REQUIRE(writer.pending() > 0);
        }
    }

    SECTION("write") {
        REQUIRE(co_await writer.write(input) == input.size());
        REQUIRE(writer.pending() > 0);
    }

    SECTION("flush") {
        REQUIRE(co_await writer.writeAll(input));
        REQUIRE(co_await writer.flush());
        REQUIRE(writer.pending() == 0);
        REQUIRE_THAT(bytesWriter->data(), Catch::Matchers::RangeEquals(input));
    }
}

static_assert(std::is_constructible_v<asyncio::BufReader<asyncio::StringReader>, asyncio::StringReader>);

static_assert(
    std::is_constructible_v<
        asyncio::BufReader<std::unique_ptr<asyncio::IReader>>,
        std::unique_ptr<asyncio::IReader>
    >
);

static_assert(
    std::is_constructible_v<
        asyncio::BufReader<std::shared_ptr<asyncio::IReader>>,
        std::shared_ptr<asyncio::IReader>
    >
);

static_assert(std::is_constructible_v<asyncio::BufWriter<asyncio::StringWriter>, asyncio::StringWriter>);

static_assert(
    std::is_constructible_v<
        asyncio::BufWriter<std::unique_ptr<asyncio::IWriter>>,
        std::unique_ptr<asyncio::IWriter>
    >
);

static_assert(
    std::is_constructible_v<
        asyncio::BufWriter<std::shared_ptr<asyncio::IWriter>>,
        std::shared_ptr<asyncio::IWriter>
    >
);

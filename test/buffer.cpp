#include "catch_extensions.h"
#include <asyncio/buffer.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

constexpr auto BUFFER_CAPACITY = 16;
constexpr std::string_view MESSAGE = "hello world\r\n";

ASYNC_TEST_CASE("buffer reader", "[buffer]") {
    static_assert(MESSAGE.size() < BUFFER_CAPACITY);
    asyncio::BufReader reader{asyncio::StringReader{std::string{MESSAGE}}, BUFFER_CAPACITY};

    SECTION("capacity") {
        REQUIRE(reader.capacity() == BUFFER_CAPACITY);
    }

    SECTION("available") {
        REQUIRE(reader.available() == 0);
    }

    SECTION("read") {
        static_assert(MESSAGE.size() > 6);

        std::string message;
        message.resize(6);

        REQUIRE(co_await reader.readExactly(std::as_writable_bytes(std::span{message})));
        REQUIRE(message == MESSAGE.substr(0, 6));
        REQUIRE(reader.available() == MESSAGE.size() - 6);

        message.resize(MESSAGE.size() - 6);
        REQUIRE(co_await reader.readExactly(std::as_writable_bytes(std::span{message})));
        REQUIRE(message == MESSAGE.substr(6));
        REQUIRE(reader.available() == 0);
    }

    SECTION("read line") {
        REQUIRE(co_await reader.readLine() == MESSAGE.substr(0, MESSAGE.size() - 2));
    }

    SECTION("read until") {
        const auto data = co_await reader.readUntil(std::byte{'\r'});
        REQUIRE(data);
        REQUIRE_THAT(
            *data,
            Catch::Matchers::RangeEquals(std::as_bytes(std::span{MESSAGE.substr(0, MESSAGE.size() - 2)}))
        );
    }

    SECTION("peek") {
        SECTION("normal") {
            static_assert(MESSAGE.size() > 6);

            std::string message;
            message.resize(6);

            REQUIRE(co_await reader.peek(std::as_writable_bytes(std::span{message})));
            REQUIRE(message == MESSAGE.substr(0, 6));
            REQUIRE(reader.available() == MESSAGE.size());
        }

        SECTION("invalid argument") {
            std::array<std::byte, 1024> data{};
            REQUIRE_ERROR(co_await reader.peek(data), std::errc::invalid_argument);
        }
    }
}

ASYNC_TEST_CASE("buffer writer", "[buffer]") {
    static_assert(MESSAGE.size() < BUFFER_CAPACITY);

    const auto stringWriter = std::make_shared<asyncio::StringWriter>();
    asyncio::BufWriter writer{stringWriter, BUFFER_CAPACITY};

    SECTION("capacity") {
        REQUIRE(writer.capacity() == BUFFER_CAPACITY);
    }

    SECTION("pending") {
        REQUIRE(writer.pending() == 0);
    }

    SECTION("write") {
        REQUIRE(co_await writer.writeAll(std::as_bytes(std::span{MESSAGE})));
        REQUIRE(writer.pending() == MESSAGE.size());
        REQUIRE(stringWriter->data().empty());

        REQUIRE(co_await writer.writeAll(std::as_bytes(std::span{MESSAGE})));
        REQUIRE(writer.pending() == MESSAGE.size() * 2 - BUFFER_CAPACITY);
        REQUIRE(stringWriter->data().size() == BUFFER_CAPACITY);
        REQUIRE(stringWriter->data().substr(0, MESSAGE.size()) == MESSAGE);
        REQUIRE(stringWriter->data().substr(MESSAGE.size()) == MESSAGE.substr(0, BUFFER_CAPACITY - MESSAGE.size()));
    }

    SECTION("flush") {
        REQUIRE(co_await writer.writeAll(std::as_bytes(std::span{MESSAGE})));
        REQUIRE(co_await writer.flush());
        REQUIRE(writer.pending() == 0);
        REQUIRE(stringWriter->data() == MESSAGE);
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

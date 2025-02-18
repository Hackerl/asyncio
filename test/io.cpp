#include "catch_extensions.h"
#include <asyncio/io.h>
#include <catch2/matchers/catch_matchers_all.hpp>

constexpr std::string_view MESSAGE = "hello world";

ASYNC_TEST_CASE("copy", "[io]") {
    asyncio::StringReader reader{std::string{MESSAGE}};
    asyncio::StringWriter writer;
    REQUIRE(co_await copy(reader, writer) == MESSAGE.size());
    REQUIRE(*writer == MESSAGE);
}

ASYNC_TEST_CASE("read all", "[io]") {
    asyncio::StringReader reader{std::string{MESSAGE}};

    const auto result = co_await reader.readAll();
    REQUIRE(result);
    REQUIRE_THAT(*result, Catch::Matchers::RangeEquals(std::as_bytes(std::span{MESSAGE})));
}

ASYNC_TEST_CASE("read exactly", "[io]") {
    SECTION("normal") {
        asyncio::StringReader reader{std::string{MESSAGE}};

        std::string message;
        message.resize(MESSAGE.size());

        REQUIRE(co_await reader.readExactly(std::as_writable_bytes(std::span{message})));
        REQUIRE(message == MESSAGE);
    }

    SECTION("unexpected eof") {
        asyncio::StringReader reader{""};

        std::string message;
        message.resize(MESSAGE.size());

        REQUIRE_ERROR(
            co_await reader.readExactly(std::as_writable_bytes(std::span{message})),
            asyncio::IOError::UNEXPECTED_EOF
        );
    }
}

ASYNC_TEST_CASE("string reader", "[io]") {
    asyncio::StringReader reader{std::string{MESSAGE}};

    std::string message;
    message.resize(MESSAGE.size());

    REQUIRE(co_await reader.read(std::as_writable_bytes(std::span{message})) == MESSAGE.size());
    REQUIRE(message == MESSAGE);

    REQUIRE(co_await reader.read(std::as_writable_bytes(std::span{message})) == 0);
}

ASYNC_TEST_CASE("string writer", "[io]") {
    asyncio::StringWriter writer;
    REQUIRE(co_await writer.writeAll(std::as_bytes(std::span{MESSAGE})));
    REQUIRE(writer.data() == MESSAGE);
    REQUIRE(*writer == MESSAGE);
}

ASYNC_TEST_CASE("bytes reader", "[io]") {
    asyncio::BytesReader reader{
        std::vector<std::byte>{
            reinterpret_cast<const std::byte *>(MESSAGE.data()),
            reinterpret_cast<const std::byte *>(MESSAGE.data()) + MESSAGE.size()
        }
    };

    std::vector<std::byte> data;
    data.resize(MESSAGE.size());

    REQUIRE(co_await reader.read(data) == MESSAGE.size());
    REQUIRE_THAT(data, Catch::Matchers::RangeEquals(std::as_bytes(std::span{MESSAGE})));

    REQUIRE(co_await reader.read(data) == 0);
}

ASYNC_TEST_CASE("bytes writer", "[io]") {
    asyncio::BytesWriter writer;
    REQUIRE(co_await writer.writeAll(std::as_bytes(std::span{MESSAGE})));
    REQUIRE_THAT(writer.data(), Catch::Matchers::RangeEquals(std::as_bytes(std::span{MESSAGE})));
    REQUIRE_THAT(*writer, Catch::Matchers::RangeEquals(std::as_bytes(std::span{MESSAGE})));
}

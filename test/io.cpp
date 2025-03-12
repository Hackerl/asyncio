#include "catch_extensions.h"
#include <asyncio/io.h>
#include <catch2/matchers/catch_matchers_all.hpp>

ASYNC_TEST_CASE("copy", "[io]") {
    const auto input = GENERATE(take(10, randomString(1, 10240)));

    asyncio::StringReader reader{input};
    asyncio::StringWriter writer;
    REQUIRE(co_await asyncio::copy(reader, writer) == input.size());
    REQUIRE(*writer == input);
}

ASYNC_TEST_CASE("read all", "[io]") {
    const auto input = GENERATE(take(10, randomString(1, 10240)));

    asyncio::StringReader reader{input};

    const auto result = co_await reader.readAll();
    REQUIRE(result);
    REQUIRE_THAT(*result, Catch::Matchers::RangeEquals(std::as_bytes(std::span{input})));
}

ASYNC_TEST_CASE("read exactly", "[io]") {
    const auto input = GENERATE(take(10, randomBytes(1, 10240)));

    SECTION("normal") {
        asyncio::BytesReader reader{input};

        std::vector<std::byte> data;
        data.resize(input.size());

        REQUIRE(co_await reader.readExactly(data));
        REQUIRE_THAT(data, Catch::Matchers::RangeEquals(input));
    }

    SECTION("unexpected eof") {
        asyncio::BytesReader reader{{}};

        std::vector<std::byte> data;
        data.resize(input.size());

        REQUIRE_ERROR(co_await reader.readExactly(data), asyncio::IOError::UNEXPECTED_EOF);
    }
}

ASYNC_TEST_CASE("string reader", "[io]") {
    const auto input = GENERATE(take(10, randomString(1, 10240)));

    asyncio::StringReader reader{input};

    std::string message;
    message.resize(input.size());

    REQUIRE(co_await reader.read(std::as_writable_bytes(std::span{message})) == input.size());
    REQUIRE(message == input);

    REQUIRE(co_await reader.read(std::as_writable_bytes(std::span{message})) == 0);
}

ASYNC_TEST_CASE("string writer", "[io]") {
    const auto input = GENERATE(take(10, randomString(1, 10240)));

    asyncio::StringWriter writer;
    REQUIRE(co_await writer.writeAll(std::as_bytes(std::span{input})));
    REQUIRE(writer.data() == input);
    REQUIRE(*writer == input);
}

ASYNC_TEST_CASE("bytes reader", "[io]") {
    const auto input = GENERATE(take(10, randomBytes(1, 10240)));

    asyncio::BytesReader reader{input};

    std::vector<std::byte> data;
    data.resize(input.size());

    REQUIRE(co_await reader.read(data) == input.size());
    REQUIRE_THAT(data, Catch::Matchers::RangeEquals(input));

    REQUIRE(co_await reader.read(data) == 0);
}

ASYNC_TEST_CASE("bytes writer", "[io]") {
    const auto input = GENERATE(take(10, randomBytes(1, 10240)));

    asyncio::BytesWriter writer;
    REQUIRE(co_await writer.writeAll(std::as_bytes(std::span{input})));
    REQUIRE_THAT(writer.data(), Catch::Matchers::RangeEquals(input));
    REQUIRE_THAT(*writer, Catch::Matchers::RangeEquals(input));
}

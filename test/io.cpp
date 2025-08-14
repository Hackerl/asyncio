#include "catch_extensions.h"
#include <asyncio/io.h>

ASYNC_TEST_CASE("copy", "[io]") {
    const auto input = GENERATE(take(10, randomBytes(1, 102400)));

    asyncio::BytesReader reader{input};
    asyncio::BytesWriter writer;
    REQUIRE(co_await asyncio::copy(reader, writer) == input.size());
    REQUIRE(*writer == input);
}

ASYNC_TEST_CASE("read all", "[io]") {
    const auto input = GENERATE(take(10, randomBytes(1, 102400)));

    asyncio::BytesReader reader{input};
    REQUIRE(co_await reader.readAll() == input);
}

ASYNC_TEST_CASE("read exactly", "[io]") {
    const auto input = GENERATE(take(10, randomBytes(1, 102400)));

    SECTION("normal") {
        asyncio::BytesReader reader{input};

        std::vector<std::byte> data;
        data.resize(input.size());

        REQUIRE(co_await reader.readExactly(data));
        REQUIRE(data == input);
    }

    SECTION("unexpected eof") {
        asyncio::BytesReader reader{{}};

        std::vector<std::byte> data;
        data.resize(input.size());

        REQUIRE_ERROR(co_await reader.readExactly(data), asyncio::IOError::UNEXPECTED_EOF);
    }
}

ASYNC_TEST_CASE("string reader", "[io]") {
    const auto input = GENERATE(take(10, randomString(1, 102400)));

    asyncio::StringReader reader{input};

    std::string message;
    message.resize(input.size());

    REQUIRE(co_await reader.read(std::as_writable_bytes(std::span{message})) == input.size());
    REQUIRE(message == input);

    REQUIRE(co_await reader.read(std::as_writable_bytes(std::span{message})) == 0);
}

ASYNC_TEST_CASE("string writer", "[io]") {
    const auto input = GENERATE(take(10, randomString(1, 102400)));

    asyncio::StringWriter writer;
    REQUIRE(co_await writer.writeAll(std::as_bytes(std::span{input})));
    REQUIRE(writer.data() == input);
    REQUIRE(*writer == input);
}

ASYNC_TEST_CASE("bytes reader", "[io]") {
    const auto input = GENERATE(take(10, randomBytes(1, 102400)));

    asyncio::BytesReader reader{input};

    std::vector<std::byte> data;
    data.resize(input.size());

    REQUIRE(co_await reader.read(data) == input.size());
    REQUIRE(data == input);

    REQUIRE(co_await reader.read(data) == 0);
}

ASYNC_TEST_CASE("bytes writer", "[io]") {
    const auto input = GENERATE(take(10, randomBytes(1, 102400)));

    asyncio::BytesWriter writer;
    REQUIRE(co_await writer.writeAll(input));
    REQUIRE(writer.data() == input);
    REQUIRE(*writer == input);
}

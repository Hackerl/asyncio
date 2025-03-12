#include "catch_extensions.h"
#include <asyncio/binary.h>

ASYNC_TEMPLATE_TEST_CASE(
    "binary transfer",
    "[binary]",
    std::int16_t, std::uint16_t, std::int32_t, std::uint32_t, std::int64_t, std::uint64_t
) {
    const auto input = GENERATE(
        take(100, random((std::numeric_limits<TestType>::min)(), (std::numeric_limits<TestType>::max)()))
    );

    SECTION("little endian") {
        asyncio::BytesWriter writer;
        REQUIRE(co_await asyncio::binary::writeLE(writer, input));

        asyncio::BytesReader reader{*std::move(writer)};
        REQUIRE(co_await asyncio::binary::readLE<TestType>(reader) == input);
    }

    SECTION("big endian") {
        asyncio::BytesWriter writer;
        REQUIRE(co_await asyncio::binary::writeBE(writer, input));

        asyncio::BytesReader reader{*std::move(writer)};
        REQUIRE(co_await asyncio::binary::readBE<TestType>(reader) == input);
    }
}

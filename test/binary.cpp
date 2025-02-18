#include "catch_extensions.h"
#include <asyncio/binary.h>
#include <catch2/generators/catch_generators_all.hpp>

template<typename T>
asyncio::task::Task<void> transfer() {
    const auto i = GENERATE(take(100, random(std::numeric_limits<T>::min(), std::numeric_limits<T>::max())));

    SECTION("little endian") {
        asyncio::BytesWriter writer;
        REQUIRE(co_await asyncio::binary::writeLE(writer, i));

        asyncio::BytesReader reader{*std::move(writer)};
        REQUIRE(co_await asyncio::binary::readLE<T>(reader) == i);
    }

    SECTION("big endian") {
        asyncio::BytesWriter writer;
        REQUIRE(co_await asyncio::binary::writeBE(writer, i));

        asyncio::BytesReader reader{*std::move(writer)};
        REQUIRE(co_await asyncio::binary::readBE<T>(reader) == i);
    }
}

ASYNC_TEST_CASE("binary transfer", "[binary]") {
    co_await transfer<std::int16_t>();
    co_await transfer<std::uint16_t>();
    co_await transfer<std::int32_t>();
    co_await transfer<std::uint32_t>();
    co_await transfer<std::int64_t>();
    co_await transfer<std::uint64_t>();
}

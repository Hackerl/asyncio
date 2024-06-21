#include <asyncio/binary.h>
#include <asyncio/pipe.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

template<typename T>
asyncio::task::Task<void> transfer(auto pipes) {
    const auto i = GENERATE(
        T{6789},
        (std::numeric_limits<T>::max)(),
        (std::numeric_limits<T>::min)()
    );

    co_await allSettled(
        [](auto reader, auto value) -> asyncio::task::Task<void> {
            auto result = co_await asyncio::binary::readLE<T>(reader);
            REQUIRE(result);
            REQUIRE(*result == value);

            result = co_await asyncio::binary::readBE<T>(reader);
            REQUIRE(result);
            REQUIRE(*result == value);
        }(std::move(pipes[0]), i),
        [](auto writer, auto value) -> asyncio::task::Task<void> {
            auto result = co_await asyncio::binary::writeLE(writer, value);
            REQUIRE(result);

            result = co_await asyncio::binary::writeBE(writer, value);
            REQUIRE(result);
        }(std::move(pipes[1]), i)
    );
}

TEST_CASE("binary transfer", "[binary]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        auto pipes = asyncio::pipe();
        REQUIRE(pipes);

        SECTION("int16_t") {
            co_await transfer<std::int16_t>(*std::move(pipes));
        }

        SECTION("uint16_t") {
            co_await transfer<std::uint16_t>(*std::move(pipes));
        }

        SECTION("int32_t") {
            co_await transfer<std::int32_t>(*std::move(pipes));
        }

        SECTION("uint32_t") {
            co_await transfer<std::uint32_t>(*std::move(pipes));
        }

        SECTION("int64_t") {
            co_await transfer<std::int64_t>(*std::move(pipes));
        }

        SECTION("uint64_t") {
            co_await transfer<std::uint64_t>(*std::move(pipes));
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

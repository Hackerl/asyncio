#include <asyncio/binary.h>
#include <asyncio/event_loop.h>
#include <asyncio/ev/pipe.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

template<typename T>
zero::async::coroutine::Task<void> transfer(auto buffers) {
    const auto i = GENERATE(
        T{6789},
        (std::numeric_limits<T>::max)(),
        (std::numeric_limits<T>::min)()
    );

    co_await allSettled(
        [](auto buffer, auto value) -> zero::async::coroutine::Task<void> {
            auto result = co_await asyncio::binary::writeLE(buffer, value);
            REQUIRE(result);

            result = co_await asyncio::binary::writeBE(buffer, value);
            REQUIRE(result);
        }(std::move(buffers[0]), i),
        [](auto buffer, auto value) -> zero::async::coroutine::Task<void> {
            auto result = co_await asyncio::binary::readLE<T>(buffer);
            REQUIRE(result);
            REQUIRE(*result == value);

            result = co_await asyncio::binary::readBE<T>(buffer);
            REQUIRE(result);
            REQUIRE(*result == value);
        }(std::move(buffers[1]), i)
    );
}

TEST_CASE("binary transfer", "[binary]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        auto buffers = asyncio::ev::pipe();
        REQUIRE(buffers);

        SECTION("int16_t") {
            co_await transfer<std::int16_t>(*std::move(buffers));
        }

        SECTION("uint16_t") {
            co_await transfer<std::uint16_t>(*std::move(buffers));
        }

        SECTION("int32_t") {
            co_await transfer<std::int32_t>(*std::move(buffers));
        }

        SECTION("uint32_t") {
            co_await transfer<std::uint32_t>(*std::move(buffers));
        }

        SECTION("int64_t") {
            co_await transfer<std::int64_t>(*std::move(buffers));
        }

        SECTION("uint64_t") {
            co_await transfer<std::uint64_t>(*std::move(buffers));
        }
    });
}

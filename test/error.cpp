#include "catch_extensions.h"
#include <asyncio/error.h>
#include <catch2/matchers/catch_matchers_all.hpp>

ASYNC_TEST_CASE("throw when task returns an error", "[error]") {
    SECTION("void") {
        SECTION("success") {
            REQUIRE_NOTHROW(
                co_await asyncio::error::guard(
                    asyncio::task::spawn([]() -> asyncio::task::Task<void, std::error_code> {
                        co_return {};
                    })
                )
            );
        }

        SECTION("failure") {
            REQUIRE_THROWS_MATCHES(
                co_await asyncio::error::guard(
                    asyncio::task::spawn([]() -> asyncio::task::Task<void, std::error_code> {
                        co_return std::unexpected{make_error_code(std::errc::invalid_argument)};
                    })
                ),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );
        }
    }

    SECTION("not void") {
        SECTION("success") {
            REQUIRE(
                co_await asyncio::error::guard(
                    asyncio::task::spawn([]() -> asyncio::task::Task<int, std::error_code> {
                        co_return 0;
                    })
                ) == 0
            );
        }

        SECTION("failure") {
            REQUIRE_THROWS_MATCHES(
                co_await asyncio::error::guard(
                    asyncio::task::spawn([]() -> asyncio::task::Task<int, std::error_code> {
                        co_return std::unexpected{make_error_code(std::errc::invalid_argument)};
                    })
                ),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );
        }
    }
}

ASYNC_TEST_CASE("catch task exception as std::expected error", "[error]") {
    SECTION("void") {
        SECTION("success") {
            REQUIRE(
                co_await asyncio::error::capture(
                    asyncio::task::spawn([]() -> asyncio::task::Task<void> {
                        co_return;
                    })
                )
            );
        }

        SECTION("failure") {
            const auto result = co_await asyncio::error::capture(
                asyncio::task::spawn([]() -> asyncio::task::Task<void> {
                    throw std::system_error{make_error_code(std::errc::invalid_argument)};
                })
            );
            REQUIRE_FALSE(result);
            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );
        }
    }

    SECTION("not void") {
        SECTION("success") {
            REQUIRE(
                co_await asyncio::error::capture(
                    asyncio::task::spawn([]() -> asyncio::task::Task<int> {
                        co_return 0;
                    })
                ) == 0
            );
        }

        SECTION("failure") {
            const auto result = co_await asyncio::error::capture(
                asyncio::task::spawn([]() -> asyncio::task::Task<int> {
                    throw std::system_error{make_error_code(std::errc::invalid_argument)};
                })
            );
            REQUIRE_FALSE(result);
            REQUIRE_THROWS_MATCHES(
                std::rethrow_exception(result.error()),
                std::system_error,
                Catch::Matchers::Predicate<std::system_error>([](const auto &error) {
                    return error.code() == std::errc::invalid_argument;
                })
            );
        }
    }
}

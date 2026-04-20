#include "catch_extensions.h"
#include <asyncio/event_loop.h>
#include <asyncio/error.h>

TEST_CASE("event loop", "[event loop]") {
    SECTION("with error") {
        SECTION("success") {
            const auto result = asyncio::run([]() -> asyncio::task::Task<int, std::error_code> {
                co_await asyncio::error::guard(asyncio::reschedule());
                co_return 1024;
            });
            REQUIRE(result == 1024);
        }

        SECTION("failure") {
            const auto result = asyncio::run([]() -> asyncio::task::Task<void, std::error_code> {
                co_await asyncio::error::guard(asyncio::reschedule());
                co_return std::unexpected{make_error_code(std::errc::invalid_argument)};
            });
            REQUIRE_ERROR(result, std::errc::invalid_argument);
        }
    }

    SECTION("with exception") {
        SECTION("success") {
            const auto result = asyncio::run([]() -> asyncio::task::Task<int> {
                co_await asyncio::error::guard(asyncio::reschedule());
                co_return 1024;
            });
            REQUIRE(result == 1024);
        }

        SECTION("failure") {
            const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
                co_await asyncio::error::guard(asyncio::reschedule());
                throw std::system_error{make_error_code(std::errc::invalid_argument)};
            });
            REQUIRE_FALSE(result);

            try {
                std::rethrow_exception(result.error());
            }
            catch (const std::system_error &error) {
                REQUIRE(error.code() == std::errc::invalid_argument);
            }
        }
    }
}

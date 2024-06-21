#include <asyncio/time.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("time", "[time]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        SECTION("sleep") {
            using namespace std::chrono_literals;
            const auto tp = std::chrono::system_clock::now();
            co_await asyncio::sleep(50ms);
            REQUIRE(std::chrono::system_clock::now() - tp > 45ms);
        }

        SECTION("timeout") {
            SECTION("success") {
                using namespace std::chrono_literals;
                const auto res = co_await asyncio::timeout(asyncio::sleep(10ms), 20ms);
                REQUIRE(res);
            }

            SECTION("timeout") {
                using namespace std::chrono_literals;
                const auto res = co_await asyncio::timeout(asyncio::sleep(20ms), 10ms);
                REQUIRE(!res);
                REQUIRE(res.error() == asyncio::TimeoutError::ELAPSED);
            }

            SECTION("failure") {
                using namespace std::chrono_literals;
                auto task = asyncio::sleep(50ms);
                REQUIRE(task.cancel());
                const auto res = co_await asyncio::timeout(std::move(task), 20ms);
                REQUIRE(res);
                REQUIRE(res.value().error() == std::errc::operation_canceled);
            }

            SECTION("cancel") {
                using namespace std::chrono_literals;
                auto task = asyncio::timeout(asyncio::sleep(20ms), 20ms);
                REQUIRE(task.cancel());
                const auto res = co_await task;
                REQUIRE(res);
                REQUIRE(!*res);
                REQUIRE(res->error() == std::errc::operation_canceled);
            }

            SECTION("cannot cancel") {
                using namespace std::chrono_literals;

                const auto promise = std::make_shared<asyncio::Promise<void, std::error_code>>();
                auto task = asyncio::timeout(
                    from(asyncio::task::Cancellable{
                        promise->getFuture(),
                        [=]() -> std::expected<void, std::error_code> {
                            return std::unexpected(asyncio::task::Error::WILL_BE_DONE);
                        }
                    }),
                    20ms
                );

                SECTION("success") {
                    co_await asyncio::sleep(20ms);
                    promise->resolve();

                    const auto &res = co_await task;
                    REQUIRE(res);
                    REQUIRE(*res);
                }

                SECTION("failure") {
                    co_await asyncio::sleep(20ms);
                    promise->reject(make_error_code(std::errc::invalid_argument));

                    const auto &res = co_await task;
                    REQUIRE(res);
                    REQUIRE(!*res);
                    REQUIRE(res->error() == std::errc::invalid_argument);
                }
            }
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

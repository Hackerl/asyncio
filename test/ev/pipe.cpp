#include <asyncio/ev/pipe.h>
#include <asyncio/event_loop.h>
#include <asyncio/error.h>
#include <zero/strings/strings.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

constexpr std::string_view MESSAGE = "hello world\r\n";

TEST_CASE("buffer pipe", "[pipe]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        auto buffers = asyncio::ev::pipe(1024);
        REQUIRE(buffers);

        SECTION("read after closing") {
            co_await allSettled(
                [](auto buffer) -> zero::async::coroutine::Task<void> {
                    auto result = co_await buffer.writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(result);

                    result = co_await buffer.flush();
                    REQUIRE(result);

                    auto line = co_await buffer.readLine();
                    REQUIRE(line);
                    REQUIRE(*line == zero::strings::trim(MESSAGE));

                    line = co_await buffer.readLine();
                    REQUIRE(!line);
                    REQUIRE(line.error() == asyncio::Error::IO_EOF);
                }(std::move(buffers->at(0))),
                [](auto buffer) -> zero::async::coroutine::Task<void> {
                    const auto line = co_await buffer.readLine();
                    REQUIRE(line);
                    REQUIRE(*line == zero::strings::trim(MESSAGE));

                    auto result = co_await buffer.writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(result);

                    result = co_await buffer.flush();
                    REQUIRE(result);

                    result = co_await buffer.close();
                    REQUIRE(result);
                }(std::move(buffers->at(1)))
            );
        }

        SECTION("write after closing") {
            co_await allSettled(
                [](auto buffer) -> zero::async::coroutine::Task<void> {
                    auto result = co_await buffer.writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(result);

                    result = co_await buffer.flush();
                    REQUIRE(result);

                    result = co_await buffer.close();
                    REQUIRE(result);
                }(std::move(buffers->at(0))),
                [](auto buffer) -> zero::async::coroutine::Task<void> {
                    const auto line = co_await buffer.readLine();
                    REQUIRE(line);
                    REQUIRE(*line == zero::strings::trim(MESSAGE));

                    co_await asyncio::sleep(10ms);
                    const auto result = co_await buffer.writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::broken_pipe);
                }(std::move(buffers->at(1)))
            );
        }

        SECTION("read timeout") {
            buffers->at(0).setTimeout(50ms, 0ms);

            std::byte data[10240];
            const auto n = co_await buffers->at(0).read(data);
            REQUIRE(!n);
            REQUIRE(n.error() == std::errc::timed_out);
        }

        SECTION("write timeout") {
            buffers->at(0).setTimeout(0ms, 500ms);

            const auto data = std::make_unique<std::byte[]>(1024 * 1024);
            const auto result = co_await buffers->at(0).writeAll({data.get(), 1024 * 1024});
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        }

        SECTION("cancel") {
            std::byte data[10240];
            const auto task = buffers->at(0).read(data);
            REQUIRE(!task.done());

            const auto result = co_await asyncio::timeout(task, 50ms);
            REQUIRE(task.done());
            REQUIRE(task.result().error() == std::errc::operation_canceled);
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        }
    });
}

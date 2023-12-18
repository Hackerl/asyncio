#include <asyncio/ev/buffer.h>
#include <asyncio/event_loop.h>
#include <asyncio/error.h>
#include <zero/strings/strings.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

constexpr std::string_view MESSAGE = "hello world\r\n";

TEST_CASE("async stream buffer", "[ev]") {
    evutil_socket_t fds[2];

#ifdef _WIN32
    REQUIRE(evutil_socketpair(AF_INET, SOCK_STREAM, 0, fds) == 0);
#else
    REQUIRE(evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
#endif

    REQUIRE(evutil_make_socket_nonblocking(fds[0]) == 0);
    REQUIRE(evutil_make_socket_nonblocking(fds[1]) == 0);

    asyncio::run([&]() -> zero::async::coroutine::Task<void> {
        std::array buffers = {
            asyncio::ev::makeBuffer(fds[0], 1024),
            asyncio::ev::makeBuffer(fds[1], 1024)
        };

        REQUIRE(buffers[0]);
        REQUIRE(buffers[1]);
        REQUIRE(buffers[0]->fd() != asyncio::INVALID_FILE_DESCRIPTOR);
        REQUIRE(buffers[1]->fd() != asyncio::INVALID_FILE_DESCRIPTOR);

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
                }(std::move(*buffers[0])),
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
                }(std::move(*buffers[1]))
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
                }(std::move(*buffers[0])),
                [](auto buffer) -> zero::async::coroutine::Task<void> {
                    const auto line = co_await buffer.readLine();
                    REQUIRE(line);
                    REQUIRE(*line == zero::strings::trim(MESSAGE));

                    co_await asyncio::sleep(10ms);
                    const auto result = co_await buffer.writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(!result);
                    REQUIRE(result.error() == std::errc::broken_pipe);
                }(std::move(*buffers[1]))
            );
        }

        SECTION("read timeout") {
            buffers[0]->setTimeout(20ms, 0ms);

            std::byte data[10240];
            const auto n = co_await buffers[0]->read(data);
            REQUIRE(!n);
            REQUIRE(n.error() == std::errc::timed_out);
        }

        SECTION("write timeout") {
            buffers[0]->setTimeout(0ms, 500ms);

            const auto data = std::make_unique<std::byte[]>(1024 * 1024);
            const auto result = co_await buffers[0]->writeAll({data.get(), 1024 * 1024});
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        }

        SECTION("cancel") {
            std::byte data[10240];
            const auto task = buffers[0]->read(data);
            REQUIRE(!task.done());

            const auto result = co_await asyncio::timeout(task, 20ms);
            REQUIRE(task.done());
            REQUIRE(task.result().error() == std::errc::operation_canceled);
            REQUIRE(!result);
            REQUIRE(result.error() == std::errc::timed_out);
        }
    });
}

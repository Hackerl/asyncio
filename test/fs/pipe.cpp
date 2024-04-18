#include <asyncio/fs/pipe.h>
#include <asyncio/event_loop.h>
#include <asyncio/error.h>
#include <catch2/catch_test_macros.hpp>

#if __unix__ || __APPLE__
#include <csignal>
#include <unistd.h>
#endif

using namespace std::chrono_literals;

TEST_CASE("pipe", "[fs]") {
#if __unix__ || __APPLE__
    signal(SIGPIPE, SIG_IGN);
#endif

    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("create") {
            auto pipes = asyncio::fs::pipe();
            REQUIRE(pipes);

            SECTION("writer closed") {
                co_await allSettled(
                    [](auto pipe) -> zero::async::coroutine::Task<void> {
                        std::byte data[11];
                        const auto result = co_await pipe.readExactly(data);
                        REQUIRE(result);
                        REQUIRE(memcmp(data, "hello world", 11) == 0);

                        const auto n = co_await pipe.read(data);
                        REQUIRE(!n);
                        REQUIRE(n.error() == asyncio::Error::IO_EOF);
                    }(std::move(pipes->at(0))),
                    [](auto pipe) -> zero::async::coroutine::Task<void> {
                        constexpr std::string_view message = "hello world";
                        auto result = co_await pipe.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);

                        result = co_await pipe.close();
                        REQUIRE(result);
                    }(std::move(pipes->at(1)))
                );
            }

            SECTION("reader closed") {
                co_await allSettled(
                    [](auto pipe) -> zero::async::coroutine::Task<void> {
                        std::byte data[11];
                        auto result = co_await pipe.readExactly(data);
                        REQUIRE(result);
                        REQUIRE(memcmp(data, "hello world", 11) == 0);

                        result = co_await pipe.close();
                        REQUIRE(result);
                    }(std::move(pipes->at(0))),
                    [](auto pipe) -> zero::async::coroutine::Task<void> {
                        constexpr std::string_view message = "hello world";
                        auto result = co_await pipe.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(result);

                        co_await asyncio::sleep(10ms);
                        result = co_await pipe.writeAll(std::as_bytes(std::span{message}));
                        REQUIRE(!result);
                        REQUIRE(result.error() == std::errc::broken_pipe);
                    }(std::move(pipes->at(1)))
                );
            }

            SECTION("cancel") {
                std::byte data[11];
                const auto result = co_await asyncio::timeout(pipes->at(0).read(data), 10ms);
                REQUIRE(!result);
                REQUIRE(result.error() == std::errc::timed_out);
            }
        }

        SECTION("from file descriptor") {
#ifdef _WIN32
            HANDLE readPipe, writePipe;
            REQUIRE(CreatePipe(&readPipe, &writePipe, nullptr, 0));

            auto reader = asyncio::fs::Pipe::from(reinterpret_cast<asyncio::FileDescriptor>(readPipe));
            REQUIRE(reader);

            auto writer = asyncio::fs::Pipe::from(reinterpret_cast<asyncio::FileDescriptor>(writePipe));
            REQUIRE(writer);
#else
            int fds[2];
            REQUIRE(pipe(fds) == 0);
            REQUIRE(evutil_make_socket_nonblocking(fds[0]) == 0);
            REQUIRE(evutil_make_socket_nonblocking(fds[1]) == 0);

            auto reader = asyncio::fs::Pipe::from(fds[0]);
            REQUIRE(reader);

            auto writer = asyncio::fs::Pipe::from(fds[1]);
            REQUIRE(writer);
#endif
            co_await allSettled(
                [](auto pipe) -> zero::async::coroutine::Task<void> {
                    std::byte data[11];
                    const auto result = co_await pipe.readExactly(data);
                    REQUIRE(result);
                    REQUIRE(memcmp(data, "hello world", 11) == 0);

                    const auto n = co_await pipe.read(data);
                    REQUIRE(!n);
                    REQUIRE(n.error() == asyncio::Error::IO_EOF);
                }(*std::move(reader)),
                [](auto pipe) -> zero::async::coroutine::Task<void> {
                    constexpr std::string_view message = "hello world";
                    auto result = co_await pipe.writeAll(std::as_bytes(std::span{message}));
                    REQUIRE(result);

                    result = co_await pipe.close();
                    REQUIRE(result);
                }(*std::move(writer))
            );
        }
    });

#if __unix__ || __APPLE__
    signal(SIGPIPE, SIG_DFL);
#endif
}

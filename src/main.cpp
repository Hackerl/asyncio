#include <asyncio/event_loop.h>
#include <zero/formatter.h>
#include <fmt/std.h>

#ifdef _WIN32
#include <zero/defer.h>
#elif __unix__ || __APPLE__
#include <csignal>
#endif

zero::async::coroutine::Task<void, std::error_code> amain(int argc, char *argv[]);

int main(const int argc, char *argv[]) {
#ifdef _WIN32
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        throw std::system_error(WSAGetLastError(), std::system_category());

    DEFER(
        if (WSACleanup() != 0)
            throw std::system_error(WSAGetLastError(), std::system_category())
    );
#elif __unix__ || __APPLE__
    signal(SIGPIPE, SIG_IGN);
#endif

    const auto result = asyncio::run([=] {
        return amain(argc, argv);
    });

    if (!result)
        throw std::system_error(result.error());

    if (!*result) {
        fmt::print(stderr, "Error: {} ({})\n", result->error().message(), result->error());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#include <asyncio/task.h>
#include <zero/formatter.h>
#include <fmt/std.h>

asyncio::task::Task<void, std::error_code> asyncMain(int argc, char *argv[]);

int main(const int argc, char *argv[]) {
#if defined(__unix__) || defined(__APPLE__)
    signal(SIGPIPE, SIG_IGN);
#endif

    const auto result = asyncio::run([=] {
        return asyncMain(argc, argv);
    });

    if (!result)
        throw std::system_error{result.error()};

    if (!*result) {
        fmt::print(stderr, "Error: {} ({})\n", result->error().message(), result->error());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

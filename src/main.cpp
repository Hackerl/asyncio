#include <asyncio/task.h>
#include <zero/formatter.h>
#include <fmt/std.h>

asyncio::task::Task<void> asyncMain(int argc, char *argv[]);

int main(const int argc, char *argv[]) {
#if defined(__unix__) || defined(__APPLE__)
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        throw zero::error::SystemError{errno, std::generic_category()};
#endif

    const auto result = asyncio::run([=] {
        return asyncMain(argc, argv);
    });

    if (!result) {
        fmt::print(stderr, "Exception: {}\n", result.error());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

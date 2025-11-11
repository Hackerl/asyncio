#include "catch_extensions.h"
#include <asyncio/process.h>
#include <zero/os/os.h>
#include <zero/strings/strings.h>
#include <catch2/matchers/catch_matchers_all.hpp>

#ifdef _WIN32
#include <asyncio/thread.h>
#endif

ASYNC_TEST_CASE("spawn child process and collect status", "[process]") {
    const auto status = co_await asyncio::process::Command{"hostname"}
                                 .stdOutput(zero::os::process::Command::StdioType::NUL)
                                 .status();
    REQUIRE(status);
    REQUIRE(status->success());
}

ASYNC_TEST_CASE("spawn child process and collect output", "[process]") {
    const auto hostname = zero::os::hostname();
    REQUIRE(hostname);

    const auto output = co_await asyncio::process::Command{"hostname"}.output();
    REQUIRE(output);
    REQUIRE(output->status.success());

    REQUIRE(zero::strings::trim({
        reinterpret_cast<const char *>(output->out.data()),
        output->out.size()
    }) == *hostname);
}

ASYNC_TEST_CASE("spawn child process with pseudo console", "[process]") {
    using namespace std::string_view_literals;

    auto pc = asyncio::process::PseudoConsole::make(80, 32);
    REQUIRE(pc);

#ifdef _WIN32
    auto child = pc->spawn(asyncio::process::Command{"cmd"});
    REQUIRE(child);
#else
    auto child = pc->spawn(asyncio::process::Command{"sh"});
    REQUIRE(child);
#endif

    auto &master = pc->master();
    auto task = master.readAll();

    REQUIRE(co_await master.writeAll(std::as_bytes(std::span{"echo hello\rexit\r"sv})));
    REQUIRE(co_await child->wait());

#ifdef _WIN32
    REQUIRE(co_await asyncio::toThreadPool([&] {
        pc->close();
    }));
#endif

    const auto data = co_await task;
    REQUIRE(data);
    REQUIRE_THAT(
        (std::string{reinterpret_cast<const char *>(data->data()), data->size()}),
        Catch::Matchers::ContainsSubstring("hello")
    );
}

#include "catch_extensions.h"
#include <asyncio/process.h>
#include <zero/os/os.h>
#include <zero/strings/strings.h>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <fmt/format.h>

ASYNC_TEST_CASE("command", "[process]") {
    SECTION("status") {
        const auto status = co_await asyncio::process::Command{"hostname"}.status();
        REQUIRE(status);
        REQUIRE(status->success());
    }

    SECTION("output") {
        SECTION("hostname") {
            const auto hostname = zero::os::hostname();
            REQUIRE(hostname);

            const auto output = co_await asyncio::process::Command{"hostname"}.output();
            REQUIRE(output);
            REQUIRE(output->status.success());
            REQUIRE(fmt::to_string(output->status) == "exit code(0)");

            REQUIRE(zero::strings::trim({
                reinterpret_cast<const char *>(output->out.data()),
                output->out.size()
            }) == *hostname);
        }

        SECTION("whoami") {
            const auto username = zero::os::username();
            REQUIRE(username);

            const auto output = co_await asyncio::process::Command{"whoami"}.output();
            REQUIRE(output);
            REQUIRE(output->status.success());
            REQUIRE(fmt::to_string(output->status) == "exit code(0)");

            REQUIRE_THAT(
                (std::string{reinterpret_cast<const char *>(output->out.data()), output->out.size()}),
                Catch::Matchers::ContainsSubstring(*username)
            );
        }
    }
}

ASYNC_TEST_CASE("pseudo console", "[process]") {
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

    auto &pipe = pc->pipe();
    REQUIRE(co_await pipe.writeAll(std::as_bytes(std::span{"echo hello\rexit\r"sv})));

    auto task = pipe.readAll();
    REQUIRE(co_await child->wait());

#ifdef _WIN32
    pc->close();
#endif

    const auto data = co_await task;
    REQUIRE(data);
    REQUIRE_THAT(
        (std::string{reinterpret_cast<const char *>(data->data()), data->size()}),
        Catch::Matchers::ContainsSubstring("hello")
    );
}

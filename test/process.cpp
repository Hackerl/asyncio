#include <asyncio/process.h>
#include <zero/os/os.h>
#include <zero/strings/strings.h>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

TEST_CASE("process", "[process]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        SECTION("status") {
            const auto status = co_await asyncio::process::Command("hostname").status();
            REQUIRE(status);
            REQUIRE(status->success());
        }

        SECTION("output") {
            SECTION("hostname") {
                const auto hostname = zero::os::hostname();
                REQUIRE(hostname);

                const auto output = co_await asyncio::process::Command("hostname").output();
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

                const auto output = co_await asyncio::process::Command("whoami").output();
                REQUIRE(output);
                REQUIRE(output->status.success());
                REQUIRE(fmt::to_string(output->status) == "exit code(0)");

                REQUIRE(zero::strings::trim({
                    reinterpret_cast<const char *>(output->out.data()),
                    output->out.size()
                }).find(*username) != std::string::npos);
            }
        }

        SECTION("pseudo console") {
            using namespace std::string_view_literals;

            auto pc = asyncio::process::PseudoConsole::make(80, 32);
            REQUIRE(pc);

#ifdef _WIN32
            auto child = pc->spawn(asyncio::process::Command("cmd"));
            REQUIRE(child);
#else
            auto child = pc->spawn(asyncio::process::Command("sh"));
            REQUIRE(child);
#endif

            auto &pipe = pc->pipe();

            const auto res = co_await pipe.writeAll(std::as_bytes(std::span{"echo hello\rexit\r"sv}));
            REQUIRE(res);

            auto task = pipe.readAll();

            const auto r = co_await child->wait();
            REQUIRE(r);

#ifdef _WIN32
            pc->close();
#endif

            const auto content = co_await task;
            REQUIRE(content);
            REQUIRE(
                std::ranges::search(
                    std::string_view{
                        reinterpret_cast<const char *>(content->data()),
                        content->size()
                    },
                    "hello"sv
                )
            );
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

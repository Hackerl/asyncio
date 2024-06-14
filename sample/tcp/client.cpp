#include <asyncio/net/stream.h>
#include <asyncio/event_loop.h>
#include <zero/cmdline.h>

using namespace std::chrono_literals;

zero::async::coroutine::Task<void, std::error_code> amain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<unsigned short>("port", "remote port");

    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<unsigned short>("port");

    auto buffer = co_await asyncio::net::connect(host, port);
    CO_EXPECT(buffer);

    while (true) {
        constexpr std::string_view message = "hello world\r\n";
        CO_EXPECT(co_await buffer->writeAll(std::as_bytes(std::span{message})));

        const auto line = co_await buffer->readLine();

        if (!line) {
            if (line.error() != asyncio::IOError::UNEXPECTED_EOF)
                co_return std::unexpected(line.error());

            break;
        }

        fmt::print("receive message[{}]\n", *line);
        co_await asyncio::sleep(1s);
    }

    co_return {};
}

#include <asyncio/net/stream.h>
#include <asyncio/buffer.h>
#include <asyncio/time.h>
#include <zero/cmdline.h>

asyncio::task::Task<void, std::error_code> asyncMain(const int argc, char *argv[]) {
    using namespace std::chrono_literals;
    using namespace std::string_view_literals;

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<unsigned short>("port", "remote port");

    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<unsigned short>("port");

    const auto stream = co_await asyncio::net::TCPStream::connect(host, port)
        .transform([](asyncio::net::TCPStream &&rhs) {
            return std::make_shared<asyncio::net::TCPStream>(std::move(rhs));
        });
    CO_EXPECT(stream);

    asyncio::BufReader reader(*stream);

    while (true) {
        CO_EXPECT(co_await stream.value()->writeAll(std::as_bytes(std::span{"hello world\r\n"sv})));

        const auto line = co_await reader.readLine();

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

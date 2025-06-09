#include <asyncio/net/stream.h>
#include <asyncio/time.h>
#include <zero/cmdline.h>

asyncio::task::Task<void, std::error_code> asyncMain(const int argc, char *argv[]) {
    using namespace std::chrono_literals;
    using namespace std::string_view_literals;

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<std::uint16_t>("port", "remote port");

    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<std::uint16_t>("port");

    auto stream = co_await asyncio::net::TCPStream::connect(host, port);
    CO_EXPECT(stream);

    while (true) {
        CO_EXPECT(co_await stream->writeAll(std::as_bytes(std::span{"hello world"sv})));

        std::string message;
        message.resize(1024);

        const auto n = co_await stream->read(std::as_writable_bytes(std::span{message}));
        CO_EXPECT(n);

        if (*n == 0)
            break;

        message.resize(*n);

        fmt::print("receive message: {}\n", message);
        CO_EXPECT(co_await asyncio::sleep(1s));
    }

    co_return {};
}

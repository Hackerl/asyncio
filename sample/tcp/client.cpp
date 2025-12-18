#include <asyncio/net/stream.h>
#include <asyncio/time.h>
#include <zero/cmdline.h>

asyncio::task::Task<void> asyncMain(const int argc, char *argv[]) {
    using namespace std::chrono_literals;
    using namespace std::string_view_literals;

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "Remote server address");
    cmdline.add<std::uint16_t>("port", "Remote server port");

    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<std::uint16_t>("port");

    auto stream = zero::error::guard(co_await asyncio::net::TCPStream::connect(host, port));

    while (true) {
        zero::error::guard(co_await stream.writeAll(std::as_bytes(std::span{"hello world"sv})));

        std::string message;
        message.resize(1024);

        const auto n = zero::error::guard(co_await stream.read(std::as_writable_bytes(std::span{message})));

        if (n == 0)
            break;

        message.resize(n);

        fmt::print("receive message: {}\n", message);
        zero::error::guard(co_await asyncio::sleep(1s));
    }
}

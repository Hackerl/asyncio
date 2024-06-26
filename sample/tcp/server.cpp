#include <asyncio/net/stream.h>
#include <asyncio/buffer.h>
#include <asyncio/signal.h>
#include <zero/cmdline.h>

asyncio::task::Task<void, std::error_code> handle(const std::shared_ptr<asyncio::net::TCPStream> stream) {
    const auto address = stream->remoteAddress();
    CO_EXPECT(address);

    fmt::print("new connection[{}]\n", fmt::to_string(*address));

    asyncio::BufReader reader(stream);

    while (true) {
        auto line = co_await reader.readLine();
        CO_EXPECT(line);

        fmt::print("receive message[{}]\n", *line);
        line->append("\r\n");
        CO_EXPECT(co_await stream->writeAll(std::as_bytes(std::span{*line})));
    }
}

asyncio::task::Task<void, std::error_code> serve(asyncio::net::TCPListener listener) {
    while (true) {
        auto stream = co_await listener.accept().transform([](asyncio::net::TCPStream &&rhs) {
            return std::make_shared<asyncio::net::TCPStream>(std::move(rhs));
        });
        CO_EXPECT(stream);

        handle(*std::move(stream)).future().fail([](const std::error_code &ec) {
            fmt::print(stderr, "unhandled error: {} ({})\n", ec.message(), ec);
        });
    }
}

asyncio::task::Task<void, std::error_code> asyncMain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<unsigned short>("port", "remote port");

    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<unsigned short>("port");

    auto listener = asyncio::net::TCPListener::listen(host, port);
    CO_EXPECT(listener);

    auto signal = asyncio::Signal::make();
    CO_EXPECT(signal);

    co_return co_await race(
        serve(*std::move(listener)),
        signal->on(SIGINT).transform([](const int) {
        })
    );
}

#include <asyncio/ev/signal.h>
#include <asyncio/net/stream.h>
#include <zero/cmdline.h>
#include <zero/formatter.h>
#include <csignal>

zero::async::coroutine::Task<void, std::error_code> handle(asyncio::net::stream::Buffer buffer) {
    fmt::print("new connection[{}]\n", fmt::to_string(*buffer.remoteAddress()));

    while (true) {
        auto line = co_await buffer.readLine();
        CO_EXPECT(line);

        fmt::print("receive message[{}]\n", *line);
        line->append("\r\n");
        CO_EXPECT(co_await buffer.writeAll(std::as_bytes(std::span{*line})));
    }
}

zero::async::coroutine::Task<void, std::error_code> serve(asyncio::net::stream::Listener listener) {
    tl::expected<void, std::error_code> result;

    while (true) {
        auto buffer = co_await listener.accept();

        if (!buffer) {
            result = tl::unexpected(buffer.error());
            break;
        }

        handle(*std::move(buffer)).future().fail([](const std::error_code &ec) {
            fmt::print(stderr, "unhandled error: {}\n", ec.message());
        });
    }

    co_return result;
}

zero::async::coroutine::Task<void, std::error_code> amain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<unsigned short>("port", "remote port");

    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<unsigned short>("port");

    auto listener = asyncio::net::stream::listen(host, port);
    CO_EXPECT(listener);

    auto signal = asyncio::ev::Signal::make(SIGINT);
    CO_EXPECT(signal);

    co_await race(signal->on(), serve(*std::move(listener)));
    co_return {};
}

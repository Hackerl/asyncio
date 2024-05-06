#include <asyncio/ev/signal.h>
#include <asyncio/net/ssl.h>
#include <zero/cmdline.h>
#include <zero/formatter.h>
#include <csignal>

zero::async::coroutine::Task<void, std::error_code> handle(asyncio::net::ssl::stream::Buffer buffer) {
    fmt::print("new connection[{}]\n", fmt::to_string(*buffer.remoteAddress()));

    while (true) {
        auto line = co_await buffer.readLine();
        CO_EXPECT(line);

        fmt::print("receive message[{}]\n", *line);
        line->append("\r\n");
        CO_EXPECT(co_await buffer.writeAll(std::as_bytes(std::span{*line})));
    }
}

zero::async::coroutine::Task<void, std::error_code> serve(asyncio::net::ssl::stream::Listener listener) {
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
    cmdline.add<std::filesystem::path>("ca", "CA cert path");
    cmdline.add<std::filesystem::path>("cert", "cert path");
    cmdline.add<std::filesystem::path>("key", "private key path");

    cmdline.addOptional("secure", 's', "verify client cert");
    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<unsigned short>("port");

    const auto ca = cmdline.get<std::filesystem::path>("ca");
    const auto cert = cmdline.get<std::filesystem::path>("cert");
    const auto privateKey = cmdline.get<std::filesystem::path>("key");

    const bool secure = cmdline.exist("secure");

    const auto context = asyncio::net::ssl::newContext({
        .ca = ca,
        .cert = cert,
        .privateKey = privateKey,
        .insecure = !secure,
        .server = true
    });
    CO_EXPECT(context);

    auto listener = asyncio::net::ssl::stream::listen(*context, host, port);
    CO_EXPECT(listener);

    auto signal = asyncio::ev::Signal::make(SIGINT);
    CO_EXPECT(signal);

    co_await race(signal->on(), serve(*std::move(listener)));
    co_return {};
}

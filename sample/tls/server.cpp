#include <asyncio/net/stream.h>
#include <asyncio/net/tls.h>
#include <asyncio/signal.h>
#include <zero/cmdline.h>

asyncio::task::Task<void, std::error_code> handle(asyncio::net::TCPStream stream, asyncio::net::tls::Context context) {
    const auto address = stream.remoteAddress();
    CO_EXPECT(address);

    fmt::print("new connection[{}]\n", fmt::to_string(*address));

    auto tls = co_await asyncio::net::tls::accept(std::move(stream), std::move(context));
    CO_EXPECT(tls);

    while (true) {
        std::string message;
        message.resize(1024);

        const auto n = co_await tls->read(std::as_writable_bytes(std::span{message}));
        CO_EXPECT(n);

        if (*n == 0)
            break;

        message.resize(*n);

        fmt::print("receive message: {}\n", message);
        CO_EXPECT(co_await tls->writeAll(std::as_bytes(std::span{message})));
    }

    co_return {};
}

asyncio::task::Task<void, std::error_code>
serve(asyncio::net::TCPListener listener, asyncio::net::tls::Context context) {
    while (true) {
        auto stream = co_await listener.accept();
        CO_EXPECT(stream);

        handle(*std::move(stream), context).future().fail([](const std::error_code &ec) {
            fmt::print(stderr, "unhandled error: {} ({})\n", ec.message(), ec);
        });
    }
}

asyncio::task::Task<void, std::error_code> asyncMain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<std::string>("ip", "listen ip");
    cmdline.add<unsigned short>("port", "listen port");
    cmdline.add<std::filesystem::path>("cert", "cert path");
    cmdline.add<std::filesystem::path>("key", "private key path");

    cmdline.addOptional("verify", '\0', "verify client cert");
    cmdline.addOptional<std::filesystem::path>("ca", '\0', "CA cert path");

    cmdline.parse(argc, argv);

    const auto ip = cmdline.get<std::string>("ip");
    const auto port = cmdline.get<unsigned short>("port");

    const auto caFile = cmdline.getOptional<std::filesystem::path>("ca");
    const auto certFile = cmdline.get<std::filesystem::path>("cert");
    const auto keyFile = cmdline.get<std::filesystem::path>("key");

    const bool verifyClient = cmdline.exist("verify");

    auto cert = asyncio::net::tls::Certificate::loadFile(certFile);
    CO_EXPECT(cert);

    auto key = asyncio::net::tls::PrivateKey::loadFile(keyFile);
    CO_EXPECT(key);

    asyncio::net::tls::ServerConfig config = {
        .verifyClient = verifyClient,
        .certKeyPairs = {{*std::move(cert), *std::move(key)}}
    };

    if (caFile) {
        auto ca = asyncio::net::tls::Certificate::loadFile(*caFile);
        CO_EXPECT(ca);
        config.rootCAs.push_back(*std::move(ca));
    }

    auto context = config.build();
    CO_EXPECT(context);

    auto listener = asyncio::net::TCPListener::listen(ip, port);
    CO_EXPECT(listener);

    auto signal = asyncio::Signal::make();
    CO_EXPECT(signal);

    co_return co_await race(
        serve(*std::move(listener), *std::move(context)),
        signal->on(SIGINT).transform([](const int) {
        })
    );
}

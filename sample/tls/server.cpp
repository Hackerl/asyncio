#include <asyncio/net/stream.h>
#include <asyncio/net/tls.h>
#include <asyncio/signal.h>
#include <zero/cmdline.h>
#include <zero/formatter.h>

asyncio::task::Task<void> handle(asyncio::net::TCPStream stream, asyncio::net::tls::Context context) {
    const auto address = zero::error::guard(stream.remoteAddress());
    fmt::print("Connection: {}\n", address);

    auto tls = zero::error::guard(co_await asyncio::net::tls::accept(std::move(stream), std::move(context)));

    while (true) {
        std::string message;
        message.resize(1024);

        const auto n = zero::error::guard(co_await tls.read(std::as_writable_bytes(std::span{message})));

        if (n == 0)
            break;

        message.resize(n);

        fmt::print("Receive message: {}\n", message);
        zero::error::guard(co_await tls.writeAll(std::as_bytes(std::span{message})));
    }

    co_return;
}

asyncio::task::Task<void>
serve(asyncio::net::TCPListener listener, const asyncio::net::tls::Context context) {
    std::expected<void, std::error_code> result;
    asyncio::task::TaskGroup group;

    while (true) {
        auto stream = co_await listener.accept();

        if (!stream) {
            result = std::unexpected{stream.error()};
            break;
        }

        auto task = handle(*std::move(stream), context);

        group.add(task);
        task.future().fail([](const auto &e) {
            fmt::print(stderr, "Unhandled exception: {}\n", e);
        });
    }

    co_await group;
    zero::error::guard(std::move(result));
}

asyncio::task::Task<void> asyncMain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<std::string>("ip", "IP address to bind");
    cmdline.add<std::uint16_t>("port", "Port number to listen on");
    cmdline.add<std::filesystem::path>("cert", "Path to server certificate file");
    cmdline.add<std::filesystem::path>("key", "Path to private key file");

    cmdline.addOptional("verify", '\0', "Enable client certificate verification");
    cmdline.addOptional<std::filesystem::path>("ca", '\0', "Path to CA certificate file");

    cmdline.parse(argc, argv);

    const auto ip = cmdline.get<std::string>("ip");
    const auto port = cmdline.get<std::uint16_t>("port");

    const auto caFile = cmdline.getOptional<std::filesystem::path>("ca");
    const auto certFile = cmdline.get<std::filesystem::path>("cert");
    const auto keyFile = cmdline.get<std::filesystem::path>("key");

    const auto verifyClient = cmdline.exist("verify");

    auto cert = zero::error::guard(co_await asyncio::net::tls::Certificate::loadFile(certFile));
    auto key = zero::error::guard(co_await asyncio::net::tls::PrivateKey::loadFile(keyFile));

    asyncio::net::tls::ServerConfig config;

    if (caFile)
        config.rootCAs({zero::error::guard(co_await asyncio::net::tls::Certificate::loadFile(*caFile))});

    auto context = zero::error::guard(
        config
        .verifyClient(verifyClient)
        .certKeyPairs({{std::move(cert), std::move(key)}})
        .build()
    );

    auto listener = zero::error::guard(asyncio::net::TCPListener::listen(ip, port));
    auto signal = asyncio::Signal::make();

    co_return co_await race(
        serve(std::move(listener), std::move(context)),
        asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
            zero::error::guard(co_await signal.on(SIGINT));
        })
    );
}

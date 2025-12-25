#include <asyncio/net/stream.h>
#include <asyncio/net/tls.h>
#include <asyncio/time.h>
#include <zero/cmdline.h>

asyncio::task::Task<void> asyncMain(const int argc, char *argv[]) {
    using namespace std::chrono_literals;
    using namespace std::string_view_literals;

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "Remote server address");
    cmdline.add<std::uint16_t>("port", "Remote server port");

    cmdline.addOptional("insecure", 'k', "Skip server certificate verification");
    cmdline.addOptional<std::filesystem::path>("ca", '\0', "Path to CA certificate file");
    cmdline.addOptional<std::filesystem::path>("cert", '\0', "Path to client certificate file");
    cmdline.addOptional<std::filesystem::path>("key", '\0', "Path to private key file");

    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<std::uint16_t>("port");

    const auto caFile = cmdline.getOptional<std::filesystem::path>("ca");
    const auto certFile = cmdline.getOptional<std::filesystem::path>("cert");
    const auto keyFile = cmdline.getOptional<std::filesystem::path>("key");

    const auto insecure = cmdline.exist("insecure");

    asyncio::net::tls::ClientConfig config;

    if (caFile)
        config.rootCAs({zero::error::guard(co_await asyncio::net::tls::Certificate::loadFile(*caFile))});

    if (certFile && keyFile) {
        auto cert = zero::error::guard(co_await asyncio::net::tls::Certificate::loadFile(*certFile));
        auto key = zero::error::guard(co_await asyncio::net::tls::PrivateKey::loadFile(*keyFile));
        config.certKeyPairs({{std::move(cert), std::move(key)}});
    }

    config.insecure(insecure);

    auto tls = zero::error::guard(
        co_await asyncio::net::tls::connect(
            zero::error::guard(co_await asyncio::net::TCPStream::connect(host, port)),
            zero::error::guard(config.build()),
            host
        )
    );

    while (true) {
        zero::error::guard(co_await tls.writeAll(std::as_bytes(std::span{"hello world"sv})));

        std::string message;
        message.resize(1024);

        const auto n = zero::error::guard(co_await tls.read(std::as_writable_bytes(std::span{message})));

        if (n == 0)
            break;

        message.resize(n);

        fmt::print("Received message: {}\n", message);
        zero::error::guard(co_await asyncio::sleep(1s));
    }
}

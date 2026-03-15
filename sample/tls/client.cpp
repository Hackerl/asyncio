#include <asyncio/net/stream.h>
#include <asyncio/net/tls.h>
#include <asyncio/time.h>
#include <asyncio/error.h>
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
        config.rootCAs({co_await asyncio::error::guard(asyncio::net::tls::Certificate::loadFile(*caFile))});

    if (certFile && keyFile) {
        auto cert = co_await asyncio::error::guard(asyncio::net::tls::Certificate::loadFile(*certFile));
        auto key = co_await asyncio::error::guard(asyncio::net::tls::PrivateKey::loadFile(*keyFile));
        config.certKeyPairs({{std::move(cert), std::move(key)}});
    }

    config.insecure(insecure);

    auto tls = co_await asyncio::error::guard(
        asyncio::net::tls::connect(
            co_await asyncio::error::guard(asyncio::net::TCPStream::connect(host, port)),
            co_await asyncio::error::guard(config.build()),
            host
        )
    );

    while (true) {
        co_await asyncio::error::guard(tls.writeAll(std::as_bytes(std::span{"hello world"sv})));

        std::string message;
        message.resize(1024);

        const auto n = co_await asyncio::error::guard(tls.read(std::as_writable_bytes(std::span{message})));

        if (n == 0)
            break;

        message.resize(n);

        fmt::print("Received message: {}\n", message);
        co_await asyncio::error::guard(asyncio::sleep(1s));
    }
}

#include <asyncio/net/stream.h>
#include <asyncio/net/tls.h>
#include <asyncio/time.h>
#include <zero/cmdline.h>

asyncio::task::Task<void, std::error_code> asyncMain(const int argc, char *argv[]) {
    using namespace std::chrono_literals;
    using namespace std::string_view_literals;

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<std::uint16_t>("port", "remote port");

    cmdline.addOptional("insecure", 'k', "skip verify server cert");
    cmdline.addOptional<std::filesystem::path>("ca", '\0', "CA cert path");
    cmdline.addOptional<std::filesystem::path>("cert", '\0', "cert path");
    cmdline.addOptional<std::filesystem::path>("key", '\0', "private key path");

    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<std::uint16_t>("port");

    const auto caFile = cmdline.getOptional<std::filesystem::path>("ca");
    const auto certFile = cmdline.getOptional<std::filesystem::path>("cert");
    const auto keyFile = cmdline.getOptional<std::filesystem::path>("key");

    const auto insecure = cmdline.exist("insecure");

    asyncio::net::tls::ClientConfig config;

    if (caFile) {
        auto ca = co_await asyncio::net::tls::Certificate::loadFile(*caFile);
        CO_EXPECT(ca);
        config.rootCAs({*std::move(ca)});
    }

    if (certFile && keyFile) {
        auto cert = co_await asyncio::net::tls::Certificate::loadFile(*certFile);
        CO_EXPECT(cert);

        auto key = co_await asyncio::net::tls::PrivateKey::loadFile(*keyFile);
        CO_EXPECT(key);

        config.certKeyPairs({{*std::move(cert), *std::move(key)}});
    }

    auto context = config
                   .insecure(insecure)
                   .build();
    CO_EXPECT(context);

    auto stream = co_await asyncio::net::TCPStream::connect(host, port);
    CO_EXPECT(stream);

    auto tls = co_await asyncio::net::tls::connect(*std::move(stream), *std::move(context), host);
    CO_EXPECT(tls);

    while (true) {
        CO_EXPECT(co_await tls->writeAll(std::as_bytes(std::span{"hello world"sv})));

        std::string message;
        message.resize(1024);

        const auto n = co_await tls->read(std::as_writable_bytes(std::span{message}));
        CO_EXPECT(n);

        if (*n == 0)
            break;

        message.resize(*n);

        fmt::print("receive message: {}\n", message);
        co_await asyncio::sleep(1s);
    }

    co_return {};
}

#include <asyncio/net/ssl.h>
#include <asyncio/event_loop.h>
#include <zero/cmdline.h>

using namespace std::chrono_literals;

asyncio::task::Task<void, std::error_code> amain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<unsigned short>("port", "remote port");

    cmdline.addOptional("insecure", 'k', "skip verify server cert");
    cmdline.addOptional<std::filesystem::path>("ca", '\0', "CA cert path");
    cmdline.addOptional<std::filesystem::path>("cert", '\0', "cert path");
    cmdline.addOptional<std::filesystem::path>("key", '\0', "private key path");

    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<unsigned short>("port");

    const auto ca = cmdline.getOptional<std::filesystem::path>("ca");
    const auto cert = cmdline.getOptional<std::filesystem::path>("cert");
    const auto privateKey = cmdline.getOptional<std::filesystem::path>("key");

    const bool insecure = cmdline.exist("insecure");

    asyncio::net::ssl::Config config = {
        .insecure = insecure,
        .server = false
    };

    if (ca)
        config.ca = *ca;

    if (cert)
        config.cert = *cert;

    if (privateKey)
        config.privateKey = *privateKey;

    const auto context = newContext(config);
    CO_EXPECT(context);

    auto buffer = co_await asyncio::net::ssl::connect(*context, host, port);
    CO_EXPECT(buffer);

    while (true) {
        constexpr std::string_view message = "hello world\r\n";
        CO_EXPECT(co_await buffer->writeAll(std::as_bytes(std::span{message})));

        const auto line = co_await buffer->readLine();

        if (!line) {
            if (line.error() != asyncio::IOError::UNEXPECTED_EOF)
                co_return std::unexpected(line.error());

            break;
        }

        fmt::print("receive message[{}]\n", *line);
        co_await asyncio::sleep(1s);
    }

    co_return {};
}

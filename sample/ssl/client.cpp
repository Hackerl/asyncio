#include <asyncio/net/ssl.h>
#include <asyncio/event_loop.h>
#include <zero/log.h>
#include <zero/cmdline.h>
#include <fmt/std.h>

#ifdef __unix__
#include <csignal>
#endif

using namespace std::chrono_literals;

int main(int argc, char *argv[]) {
    INIT_CONSOLE_LOG(zero::INFO_LEVEL);

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

    bool insecure = cmdline.exist("insecure");

#ifdef _WIN32
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup failed");
        return -1;
    }
#endif

#ifdef __unix__
    signal(SIGPIPE, SIG_IGN);
#endif

    asyncio::run([&]() -> zero::async::coroutine::Task<void> {
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

        if (!context) {
            LOG_ERROR("create ssl context failed[{}]", context.error());
            co_return;
        }

        auto buffer = std::move(co_await asyncio::net::ssl::stream::connect(*context, host, port));

        if (!buffer) {
            LOG_ERROR("stream buffer connect failed[{}]", buffer.error());
            co_return;
        }

        while (true) {
            std::string message = "hello world\r\n";

            if (const auto result = co_await buffer->writeAll(std::as_bytes(std::span{message})); !result) {
                LOG_ERROR("stream buffer drain failed[{}]", result.error());
                break;
            }

            const auto line = co_await buffer->readLine();

            if (!line) {
                LOG_ERROR("stream buffer read line failed[{}]", line.error());
                break;
            }

            LOG_INFO("receive message[{}]", *line);
            co_await asyncio::sleep(1s);
        }
    });

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}

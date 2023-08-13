#include <zero/log.h>
#include <zero/cmdline.h>
#include <asyncio/net/ssl.h>
#include <asyncio/event_loop.h>

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

    auto host = cmdline.get<std::string>("host");
    auto port = cmdline.get<unsigned short>("port");

    auto ca = cmdline.getOptional<std::filesystem::path>("ca");
    auto cert = cmdline.getOptional<std::filesystem::path>("cert");
    auto privateKey = cmdline.getOptional<std::filesystem::path>("key");

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

        auto context = asyncio::net::ssl::newContext(config);

        if (!context) {
            LOG_ERROR("create ssl context failed[%s]", context.error().message().c_str());
            co_return;
        }

        auto result = co_await asyncio::net::ssl::stream::connect(*context, host, port);

        if (!result) {
            LOG_ERROR("ssl stream buffer connect failed[%s]", result.error().message().c_str());
            co_return;
        }

        auto &buffer = *result;

        while (true) {
            buffer->writeLine("hello world");
            auto res = co_await buffer->drain();

            if (!res) {
                LOG_ERROR("ssl stream buffer drain failed[%s]", res.error().message().c_str());
                break;
            }

            auto line = co_await buffer->readLine();

            if (!line) {
                LOG_ERROR("ssl stream buffer read line failed[%s]", line.error().message().c_str());
                break;
            }

            LOG_INFO("receive message[%s]", line->c_str());
            co_await asyncio::sleep(1s);
        }
    });

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
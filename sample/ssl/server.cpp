#include <zero/log.h>
#include <zero/cmdline.h>
#include <asyncio/ev/signal.h>
#include <asyncio/net/ssl.h>
#include <asyncio/event_loop.h>
#include <csignal>

zero::async::coroutine::Task<void> handle(std::shared_ptr<asyncio::net::stream::IBuffer> buffer) {
    LOG_INFO("new connection[%s]", asyncio::net::stringify(*buffer->remoteAddress()).c_str());

    while (true) {
        auto line = co_await buffer->readLine();

        if (!line) {
            LOG_ERROR("stream buffer read line failed[%s]", line.error().message().c_str());
            break;
        }

        LOG_INFO("receive message[%s]", line->c_str());

        buffer->writeLine(*line);
        auto res = co_await buffer->drain();

        if (!res) {
            LOG_ERROR("stream buffer drain failed[%s]", res.error().message().c_str());
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    INIT_CONSOLE_LOG(zero::INFO_LEVEL);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<unsigned short>("port", "remote port");
    cmdline.add<std::filesystem::path>("ca", "CA cert path");
    cmdline.add<std::filesystem::path>("cert", "cert path");
    cmdline.add<std::filesystem::path>("key", "private key path");

    cmdline.addOptional("secure", 's', "verify client cert");
    cmdline.parse(argc, argv);

    auto host = cmdline.get<std::string>("host");
    auto port = cmdline.get<unsigned short>("port");

    auto ca = cmdline.get<std::filesystem::path>("ca");
    auto cert = cmdline.get<std::filesystem::path>("cert");
    auto privateKey = cmdline.get<std::filesystem::path>("key");

    bool secure = cmdline.exist("secure");

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
                .ca = ca,
                .cert = cert,
                .privateKey = privateKey,
                .insecure = !secure,
                .server = true
        };

        auto context = asyncio::net::ssl::newContext(config);

        if (!context) {
            LOG_ERROR("create ssl context failed[%s]", context.error().message().c_str());
            co_return;
        }

        auto listener = asyncio::net::ssl::stream::listen(*context, host, port);

        if (!listener) {
            LOG_ERROR("listen failed[%s]", listener.error().message().c_str());
            co_return;
        }

        auto signal = asyncio::ev::makeSignal(SIGINT);

        if (!signal) {
            LOG_ERROR("make signal failed[%s]", signal.error().message().c_str());
            co_return;
        }

        co_await zero::async::coroutine::allSettled(
                [&]() -> zero::async::coroutine::Task<void> {
                    co_await signal->on();
                    listener->close();
                }(),
                [&]() -> zero::async::coroutine::Task<void> {
                    while (true) {
                        auto result = co_await listener->accept();

                        if (!result) {
                            LOG_ERROR("accept failed[%s]", result.error().message().c_str());
                            break;
                        }

                        handle(*result);
                    }
                }()
        );
    });

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
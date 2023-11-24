#include <asyncio/ev/signal.h>
#include <asyncio/net/ssl.h>
#include <asyncio/event_loop.h>
#include <zero/log.h>
#include <zero/cmdline.h>
#include <csignal>
#include <fmt/std.h>

zero::async::coroutine::Task<void> handle(asyncio::net::ssl::stream::Buffer buffer) {
    LOG_INFO("new connection[{}]", fmt::to_string(*buffer.remoteAddress()));

    while (true) {
        const auto line = co_await buffer.readLine();

        if (!line) {
            LOG_ERROR("stream buffer read line failed[{}]", line.error());
            break;
        }

        LOG_INFO("receive message[{}]", *line);

        if (const auto result = co_await buffer.writeAll(std::as_bytes(std::span{*line})); !result) {
            LOG_ERROR("stream buffer drain failed[{}]", result.error());
            break;
        }
    }
}

zero::async::coroutine::Task<void, std::error_code> serve(asyncio::net::ssl::stream::Listener listener) {
    tl::expected<void, std::error_code> result;

    while (true) {
        auto buffer = std::move(co_await listener.accept());

        if (!buffer) {
            result = tl::unexpected(buffer.error());
            break;
        }

        handle(std::move(*buffer));
    }

    co_return result;
}

int main(const int argc, char *argv[]) {
    INIT_CONSOLE_LOG(zero::INFO_LEVEL);

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
        const auto context = asyncio::net::ssl::newContext({
            .ca = ca,
            .cert = cert,
            .privateKey = privateKey,
            .insecure = !secure,
            .server = true
        });

        if (!context) {
            LOG_ERROR("create ssl context failed[{}]", context.error());
            co_return;
        }

        auto listener = asyncio::net::ssl::stream::listen(*context, host, port);

        if (!listener) {
            LOG_ERROR("listen failed[{}]", listener.error());
            co_return;
        }

        auto signal = asyncio::ev::makeSignal(SIGINT);

        if (!signal) {
            LOG_ERROR("make signal failed[{}]", signal.error());
            co_return;
        }

        co_await race(signal->on(), serve(std::move(*listener)));
    });

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}

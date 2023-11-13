#include <asyncio/ev/signal.h>
#include <asyncio/net/stream.h>
#include <asyncio/event_loop.h>
#include <zero/log.h>
#include <zero/cmdline.h>
#include <csignal>
#include <fmt/std.h>

zero::async::coroutine::Task<void> handle(asyncio::net::stream::Buffer buffer) {
    LOG_INFO("new connection[{}]", fmt::to_string(*buffer.remoteAddress()));

    while (true) {
        auto line = co_await buffer.readLine();

        if (!line) {
            LOG_ERROR("stream buffer read line failed[{}]", line.error());
            break;
        }

        LOG_INFO("receive message[{}]", *line);

        auto result = co_await buffer.writeAll(std::as_bytes(std::span{*line}));

        if (!result) {
            LOG_ERROR("stream buffer drain failed[{}]", result.error());
            break;
        }
    }
}

zero::async::coroutine::Task<void, std::error_code> serve(asyncio::net::stream::Listener listener) {
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

int main(int argc, char *argv[]) {
    INIT_CONSOLE_LOG(zero::INFO_LEVEL);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<unsigned short>("port", "remote port");

    cmdline.parse(argc, argv);

    auto host = cmdline.get<std::string>("host");
    auto port = cmdline.get<unsigned short>("port");

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
        auto listener = asyncio::net::stream::listen(host, port);

        if (!listener) {
            LOG_ERROR("listen failed[{}]", listener.error());
            co_return;
        }

        auto signal = asyncio::ev::makeSignal(SIGINT);

        if (!signal) {
            LOG_ERROR("make signal failed[{}]", signal.error());
            co_return;
        }

        co_await zero::async::coroutine::race(signal->on(), serve(std::move(*listener)));
    });

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
#include <asyncio/net/stream.h>
#include <asyncio/event_loop.h>
#include <zero/log.h>
#include <zero/cmdline.h>
#include <fmt/std.h>

#if __unix__ || __APPLE__
#include <csignal>
#endif

using namespace std::chrono_literals;

int main(const int argc, char *argv[]) {
    INIT_CONSOLE_LOG(zero::INFO_LEVEL);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<unsigned short>("port", "remote port");

    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<unsigned short>("port");

#ifdef _WIN32
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup failed");
        return -1;
    }
#endif

#if __unix__ || __APPLE__
    signal(SIGPIPE, SIG_IGN);
#endif

    asyncio::run([&]() -> zero::async::coroutine::Task<void> {
        auto buffer = std::move(co_await asyncio::net::stream::connect(host, port));

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

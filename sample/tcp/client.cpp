#include <asyncio/net/stream.h>
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
        auto buffer = std::move(co_await asyncio::net::stream::connect(host, port));

        if (!buffer) {
            LOG_ERROR("stream buffer connect failed[{}]", buffer.error());
            co_return;
        }

        while (true) {
            buffer->writeLine("hello world");
            auto res = co_await buffer->drain();

            if (!res) {
                LOG_ERROR("stream buffer drain failed[{}]", res.error());
                break;
            }

            auto line = co_await buffer->readLine();

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
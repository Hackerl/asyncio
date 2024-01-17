#include <asyncio/http/websocket.h>
#include <asyncio/event_loop.h>
#include <zero/log.h>
#include <zero/cmdline.h>
#include <zero/encoding/hex.h>
#include <fmt/std.h>

#if __unix__ || __APPLE__
#include <csignal>
#endif

using namespace std::chrono_literals;

int main(int argc, char *argv[]) {
    INIT_CONSOLE_LOG(zero::INFO_LEVEL);

    zero::Cmdline cmdline;

    cmdline.add<asyncio::http::URL>("url", "websocket url");
    cmdline.parse(argc, argv);

    const auto url = cmdline.get<asyncio::http::URL>("url");

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
        auto ws = co_await asyncio::http::ws::connect(url);

        if (!ws) {
            LOG_ERROR("ws connect failed[{}]", ws.error());
            co_return;
        }

        while (true) {
            auto message = co_await ws->readMessage();

            if (!message) {
                LOG_ERROR("ws read message failed[{}]", message.error());
                break;
            }

            switch (message->opcode) {
            case asyncio::http::ws::TEXT:
                LOG_INFO("receive text message: {}", std::get<std::string>(message->data));
                break;

            case asyncio::http::ws::BINARY:
                LOG_INFO(
                    "receive binary message: {}",
                    zero::encoding::hex::encode(std::get<std::vector<std::byte>>(message->data))
                );
                break;

            default:
                std::abort();
            }

            if (const auto result = co_await ws->writeMessage(std::move(*message)); !result) {
                LOG_ERROR("ws write message failed[{}]", result.error());
                break;
            }
        }
    });

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}

#include <asyncio/http/websocket.h>
#include <zero/cmdline.h>
#include <zero/encoding/hex.h>

zero::async::coroutine::Task<void, std::error_code> amain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<asyncio::http::URL>("url", "websocket url");
    cmdline.parse(argc, argv);

    const auto url = cmdline.get<asyncio::http::URL>("url");

    auto ws = co_await asyncio::http::ws::connect(url);
    CO_EXPECT(ws);

    while (true) {
        auto message = co_await ws->readMessage();

        if (!message) {
            if (message.error() != asyncio::http::ws::WebSocket::CloseCode::NORMAL_CLOSURE)
                co_return tl::unexpected(message.error());

            break;
        }

        switch (message->opcode) {
        case asyncio::http::ws::TEXT:
            fmt::print("receive text message: {}\n", std::get<std::string>(message->data));
            break;

        case asyncio::http::ws::BINARY:
            fmt::print(
                "receive binary message: {}\n",
                zero::encoding::hex::encode(std::get<std::vector<std::byte>>(message->data))
            );
            break;

        default:
            std::abort();
        }

        CO_EXPECT(co_await ws->writeMessage(*std::move(message)));
    }

    co_return tl::expected<void, std::error_code>{};
}

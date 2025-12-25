#include <asyncio/http/websocket.h>
#include <zero/cmdline.h>
#include <zero/encoding/hex.h>

asyncio::task::Task<void> asyncMain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<asyncio::http::URL>("url", "WebSocket server URL");
    cmdline.parse(argc, argv);

    const auto url = cmdline.get<asyncio::http::URL>("url");

    auto ws = zero::error::guard(co_await asyncio::http::ws::WebSocket::connect(url));

    while (true) {
        auto message = co_await ws.readMessage();

        if (!message) {
            if (message.error() != asyncio::http::ws::CloseCode::NORMAL_CLOSURE)
                throw zero::error::SystemError{message.error()};

            break;
        }

        switch (message->opcode) {
        case asyncio::http::ws::Opcode::TEXT:
            fmt::print("Received text message: {}\n", std::get<std::string>(message->data));
            break;

        case asyncio::http::ws::Opcode::BINARY:
            fmt::print(
                "Received binary message: {}\n",
                zero::encoding::hex::encode(std::get<std::vector<std::byte>>(message->data))
            );
            break;

        default:
            std::abort();
        }

        zero::error::guard(co_await ws.writeMessage(*std::move(message)));
    }
}

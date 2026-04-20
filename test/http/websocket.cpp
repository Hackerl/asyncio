#include <catch_extensions.h>
#include <asyncio/http/websocket.h>
#include <asyncio/net/stream.h>
#include <asyncio/buffer.h>
#include <asyncio/binary.h>
#include <zero/encoding/base64.h>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <openssl/sha.h>
#include <regex>

constexpr auto WebSocketMagic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr auto MaskingKeyLength = 4;

constexpr auto TwoBytePayloadLength = 126;
constexpr auto EightBytePayloadLength = 127;

constexpr auto MaxSingleBytePayloadLength = 125;
constexpr auto MaxTwoBytePayloadLength = (std::numeric_limits<std::uint16_t>::max)();

namespace {
    class Server {
    public:
        explicit Server(asyncio::net::TCPStream stream) : mStream{std::move(stream)} {
        }

        static asyncio::task::Task<Server> accept(asyncio::net::TCPStream stream) {
            std::string rawHeader;

            while (true) {
                std::array<std::byte, 1024> data{};
                const auto n = co_await asyncio::error::guard(stream.read(data));

                rawHeader.append(reinterpret_cast<const char *>(data.data()), n);

                if (rawHeader.ends_with("\r\n\r\n"))
                    break;
            }

            std::smatch match;

            if (!std::regex_search(rawHeader, match, std::regex(R"(Sec-WebSocket-Key: (.+))")))
                throw co_await asyncio::error::StacktraceError<std::runtime_error>::make("No websocket key header");

            std::array<std::byte, SHA_DIGEST_LENGTH> digest{};
            const auto data = match.str(1) + WebSocketMagic;

            SHA1(
                reinterpret_cast<const unsigned char *>(data.data()),
                data.size(),
                reinterpret_cast<unsigned char *>(digest.data())
            );

            const auto response = fmt::format(
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: {}\r\n\r\n",
                zero::encoding::base64::encode(digest)
            );

            co_await asyncio::error::guard(stream.writeAll(std::as_bytes(std::span{response})));
            co_return Server{std::move(stream)};
        }

        asyncio::task::Task<std::pair<asyncio::http::ws::Opcode, std::vector<std::byte>>> readMessage() {
            asyncio::http::ws::Header header;

            co_await asyncio::error::guard(
                mStream.readExactly({reinterpret_cast<std::byte *>(&header), sizeof(header)})
            );

            std::vector<std::byte> payload;

            if (const auto length = header.length(); length == EightBytePayloadLength)
                payload.resize(co_await asyncio::error::guard(asyncio::binary::readBE<std::uint64_t>(mStream)));
            else if (length == TwoBytePayloadLength)
                payload.resize(co_await asyncio::error::guard(asyncio::binary::readBE<std::uint16_t>(mStream)));
            else
                payload.resize(length);

            std::array<std::byte, MaskingKeyLength> key{};

            co_await asyncio::error::guard(mStream.readExactly(std::as_writable_bytes(std::span{key})));
            co_await asyncio::error::guard(mStream.readExactly(payload));

            for (std::size_t i{0}; i < payload.size(); ++i)
                payload[i] ^= key[i % 4];

            co_return std::pair{header.opcode(), std::move(payload)};
        }

        asyncio::task::Task<void>
        writeMessage(const asyncio::http::ws::Opcode opcode, const std::span<const std::byte> payload) {
            asyncio::http::ws::Header header;

            header.final(true);
            header.mask(false);
            header.opcode(opcode);

            std::size_t extendedBytes{};
            const auto length = payload.size();

            if (length > MaxTwoBytePayloadLength) {
                extendedBytes = 8;
                header.length(EightBytePayloadLength);
            }
            else if (length > MaxSingleBytePayloadLength) {
                extendedBytes = 2;
                header.length(TwoBytePayloadLength);
            }
            else {
                header.length(length);
            }

            co_await asyncio::error::guard(
                mStream.writeAll({reinterpret_cast<const std::byte *>(&header), sizeof(header)})
            );

            if (extendedBytes == 2)
                co_await asyncio::error::guard(asyncio::binary::writeBE(mStream, static_cast<std::uint16_t>(length)));
            else if (extendedBytes == 8)
                co_await asyncio::error::guard(asyncio::binary::writeBE<std::uint64_t>(mStream, length));

            co_await asyncio::error::guard(mStream.writeAll(payload));
        }

    private:
        asyncio::net::TCPStream mStream;
    };
}

ASYNC_TEST_CASE("websocket", "[http::websocket]") {
    auto listener = co_await asyncio::error::guard(asyncio::net::TCPListener::listen("127.0.0.1", 0));
    const auto address = co_await asyncio::error::guard(listener.address());

    auto url = co_await asyncio::error::guard(asyncio::http::URL::from("http://127.0.0.1"));
    url.port(std::get<asyncio::net::IPv4Address>(address).port);

    auto [server, ws] = co_await all(
        asyncio::task::spawn([&]() -> asyncio::task::Task<Server> {
            auto stream = co_await asyncio::error::guard(listener.accept());
            co_return co_await Server::accept(std::move(stream));
        }),
        asyncio::task::spawn([&]() -> asyncio::task::Task<asyncio::http::ws::WebSocket> {
            co_return co_await asyncio::error::guard(asyncio::http::ws::WebSocket::connect(url));
        })
    );

    SECTION("send text") {
        const auto payload = GENERATE(take(1, randomString(1, 102400)));
        auto task = ws.sendText(payload);

        const auto [opcode, data] = co_await server.readMessage();
        REQUIRE(opcode == asyncio::http::ws::Opcode::Text);
        REQUIRE(std::string_view{reinterpret_cast<const char *>(data.data()), data.size()} == payload);

        REQUIRE(co_await task);
    }

    SECTION("send binary") {
        const auto payload = GENERATE(take(1, randomBytes(1, 102400)));
        auto task = ws.sendBinary(payload);

        const auto [opcode, data] = co_await server.readMessage();
        REQUIRE(opcode == asyncio::http::ws::Opcode::Binary);
        REQUIRE(data == payload);

        REQUIRE(co_await task);
    }

    SECTION("read message") {
        SECTION("text") {
            const auto payload = GENERATE(take(1, randomString(1, 102400)));
            auto task = ws.readMessage();

            co_await server.writeMessage(asyncio::http::ws::Opcode::Text, std::as_bytes(std::span{payload}));

            const auto message = co_await task;
            REQUIRE(message);
            REQUIRE(message->opcode == asyncio::http::ws::Opcode::Text);
            REQUIRE(std::get<std::string>(message->data) == payload);
        }

        SECTION("binary") {
            const auto payload = GENERATE(take(1, randomBytes(1, 102400)));
            auto task = ws.readMessage();

            co_await server.writeMessage(asyncio::http::ws::Opcode::Binary, payload);

            const auto message = co_await task;
            REQUIRE(message);
            REQUIRE(message->opcode == asyncio::http::ws::Opcode::Binary);
            REQUIRE(std::get<std::vector<std::byte>>(message->data) == payload);
        }
    }

    SECTION("client close") {
        auto task = ws.close(asyncio::http::ws::CloseCode::NormalClosure);

        const auto [opcode, data] = co_await server.readMessage();
        REQUIRE(opcode == asyncio::http::ws::Opcode::Close);

        REQUIRE(
            static_cast<asyncio::http::ws::CloseCode>(ntohs(*reinterpret_cast<const std::uint16_t *>(data.data()))) ==
            asyncio::http::ws::CloseCode::NormalClosure
        );

        co_await server.writeMessage(opcode, data);
        REQUIRE(co_await task);
    }

    SECTION("server close") {
        auto task = ws.readMessage();

        const auto code = htons(std::to_underlying(asyncio::http::ws::CloseCode::NormalClosure));

        co_await server.writeMessage(
            asyncio::http::ws::Opcode::Close,
            {reinterpret_cast<const std::byte *>(&code), sizeof(code)}
        );

        REQUIRE_ERROR(co_await task, asyncio::http::ws::CloseCode::NormalClosure);
    }
}

ASYNC_TEST_CASE("message deflate", "[http::websocket]") {
    const auto windowBits = GENERATE(9, 10, 11, 12, 13, 14, 15);

    auto compressor = co_await asyncio::error::guard(asyncio::http::ws::Compressor::make(windowBits));
    auto decompressor = co_await asyncio::error::guard(asyncio::http::ws::Decompressor::make(windowBits));

    const auto times = GENERATE(take(1, random(1, 64)));

    for (int i{0}; i < times; ++i) {
        const auto input = GENERATE(take(1, randomBytes(1, 102400)));

        const auto compressed = co_await compressor.compress(input);
        REQUIRE(compressed);
        REQUIRE(co_await decompressor.decompress(*compressed) == input);
    }
}

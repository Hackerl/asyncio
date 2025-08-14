#include <catch_extensions.h>
#include <asyncio/http/websocket.h>
#include <asyncio/net/stream.h>
#include <asyncio/buffer.h>
#include <asyncio/binary.h>
#include <zero/encoding/base64.h>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <openssl/sha.h>
#include <regex>

constexpr auto WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr auto MASKING_KEY_LENGTH = 4;

constexpr auto TWO_BYTE_PAYLOAD_LENGTH = 126;
constexpr auto EIGHT_BYTE_PAYLOAD_LENGTH = 127;

constexpr auto MAX_SINGLE_BYTE_PAYLOAD_LENGTH = 125;
constexpr auto MAX_TWO_BYTE_PAYLOAD_LENGTH = (std::numeric_limits<std::uint16_t>::max)();

namespace {
    class Server {
    public:
        DEFINE_ERROR_CODE_INNER(
            Error,
            "WebsocketServer",
            NO_KEY_HEADER, "no websocket key header"
        )

        explicit Server(asyncio::net::TCPStream stream) : mStream{std::move(stream)} {
        }

        static asyncio::task::Task<Server, std::error_code> accept(asyncio::net::TCPStream stream) {
            std::string rawHeader;

            while (true) {
                std::array<std::byte, 1024> data{};
                const auto n = co_await stream.read(data);
                CO_EXPECT(n);

                rawHeader.append(reinterpret_cast<const char *>(data.data()), *n);

                if (rawHeader.ends_with("\r\n\r\n"))
                    break;
            }

            std::smatch match;

            if (!std::regex_search(rawHeader, match, std::regex(R"(Sec-WebSocket-Key: (.+))")))
                co_return std::unexpected{make_error_code(Error::NO_KEY_HEADER)};

            std::array<std::byte, SHA_DIGEST_LENGTH> digest{};
            const auto data = match.str(1) + WS_MAGIC;

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

            CO_EXPECT(co_await stream.writeAll(std::as_bytes(std::span{response})));
            co_return Server{std::move(stream)};
        }

        asyncio::task::Task<std::pair<asyncio::http::ws::Opcode, std::vector<std::byte>>, std::error_code>
        readMessage() {
            asyncio::http::ws::Header header;

            CO_EXPECT(co_await mStream.readExactly({reinterpret_cast<std::byte *>(&header), sizeof(header)}));

            std::vector<std::byte> payload;

            if (const auto length = header.length(); length == EIGHT_BYTE_PAYLOAD_LENGTH) {
                const auto n = co_await asyncio::binary::readBE<std::uint64_t>(mStream);
                CO_EXPECT(n);
                payload.resize(*n);
            }
            else if (length == TWO_BYTE_PAYLOAD_LENGTH) {
                const auto n = co_await asyncio::binary::readBE<std::uint16_t>(mStream);
                CO_EXPECT(n);
                payload.resize(*n);
            }
            else {
                payload.resize(length);
            }

            std::array<std::byte, MASKING_KEY_LENGTH> key{};
            CO_EXPECT(co_await mStream.readExactly(std::as_writable_bytes(std::span{key})));
            CO_EXPECT(co_await mStream.readExactly(payload));

            for (std::size_t i{0}; i < payload.size(); ++i)
                payload[i] ^= key[i % 4];

            co_return std::pair{header.opcode(), std::move(payload)};
        }

        asyncio::task::Task<void, std::error_code>
        writeMessage(const asyncio::http::ws::Opcode opcode, const std::span<const std::byte> payload) {
            asyncio::http::ws::Header header;

            header.final(true);
            header.mask(false);
            header.opcode(opcode);

            std::size_t extendedBytes{};
            const auto length = payload.size();

            if (length > MAX_TWO_BYTE_PAYLOAD_LENGTH) {
                extendedBytes = 8;
                header.length(EIGHT_BYTE_PAYLOAD_LENGTH);
            }
            else if (length > MAX_SINGLE_BYTE_PAYLOAD_LENGTH) {
                extendedBytes = 2;
                header.length(TWO_BYTE_PAYLOAD_LENGTH);
            }
            else {
                header.length(length);
            }

            CO_EXPECT(co_await mStream.writeAll({reinterpret_cast<const std::byte *>(&header), sizeof(header)}));

            if (extendedBytes == 2) {
                CO_EXPECT(co_await asyncio::binary::writeBE(mStream, static_cast<std::uint16_t>(length)));
            }
            else if (extendedBytes == 8) {
                CO_EXPECT(co_await asyncio::binary::writeBE<std::uint64_t>(mStream, length));
            }

            co_return co_await mStream.writeAll(payload);
        }

    private:
        asyncio::net::TCPStream mStream;
    };
}

DECLARE_ERROR_CODE(Server::Error)

DEFINE_ERROR_CATEGORY_INSTANCE(Server::Error)

ASYNC_TEST_CASE("websocket", "[http::websocket]") {
    auto listener = asyncio::net::TCPListener::listen("127.0.0.1", 0);
    REQUIRE(listener);

    const auto address = listener->address();
    REQUIRE(address);

    auto url = asyncio::http::URL::from("http://127.0.0.1");
    REQUIRE(url);

    url->port(std::get<asyncio::net::IPv4Address>(*address).port);

    auto result = co_await all(
        listener->accept().andThen(Server::accept),
        asyncio::http::ws::WebSocket::connect(*url)
    );
    REQUIRE(result);

    auto &server = std::get<0>(*result);
    auto &ws = std::get<1>(*result);

    SECTION("send text") {
        const auto payload = GENERATE(take(1, randomString(1, 102400)));
        auto task = ws.sendText(payload);

        const auto message = co_await server.readMessage();
        REQUIRE(message);
        REQUIRE(message->first == asyncio::http::ws::Opcode::TEXT);
        REQUIRE(
            std::string_view{reinterpret_cast<const char *>(message->second.data()), message->second.size()}
            == payload
        );

        REQUIRE(co_await task);
    }

    SECTION("send binary") {
        const auto payload = GENERATE(take(1, randomBytes(1, 102400)));
        auto task = ws.sendBinary(payload);

        const auto message = co_await server.readMessage();
        REQUIRE(message);
        REQUIRE(message->first == asyncio::http::ws::Opcode::BINARY);
        REQUIRE(message->second == payload);

        REQUIRE(co_await task);
    }

    SECTION("read message") {
        SECTION("text") {
            const auto payload = GENERATE(take(1, randomString(1, 102400)));
            auto task = ws.readMessage();

            REQUIRE(co_await server.writeMessage(asyncio::http::ws::Opcode::TEXT, std::as_bytes(std::span{payload})));

            const auto message = co_await task;
            REQUIRE(message);
            REQUIRE(message->opcode == asyncio::http::ws::Opcode::TEXT);
            REQUIRE(std::get<std::string>(message->data) == payload);
        }

        SECTION("binary") {
            const auto payload = GENERATE(take(1, randomBytes(1, 102400)));
            auto task = ws.readMessage();

            REQUIRE(co_await server.writeMessage(asyncio::http::ws::Opcode::BINARY, payload));

            const auto message = co_await task;
            REQUIRE(message);
            REQUIRE(message->opcode == asyncio::http::ws::Opcode::BINARY);
            REQUIRE(std::get<std::vector<std::byte>>(message->data) == payload);
        }
    }

    SECTION("client close") {
        auto task = ws.close(asyncio::http::ws::CloseCode::NORMAL_CLOSURE);

        const auto message = co_await server.readMessage();
        REQUIRE(message);
        REQUIRE(message->first == asyncio::http::ws::Opcode::CLOSE);

        REQUIRE(
            static_cast<asyncio::http::ws::CloseCode>(
                ntohs(*reinterpret_cast<const std::uint16_t *>(message->second.data()))
            ) == asyncio::http::ws::CloseCode::NORMAL_CLOSURE
        );

        REQUIRE(co_await server.writeMessage(message->first, message->second));
        REQUIRE(co_await task);
    }

    SECTION("server close") {
        auto task = ws.readMessage();

        const auto code = htons(std::to_underlying(asyncio::http::ws::CloseCode::NORMAL_CLOSURE));

        REQUIRE(co_await server.writeMessage(
            asyncio::http::ws::Opcode::CLOSE,
            {reinterpret_cast<const std::byte *>(&code), sizeof(code)}
        ));

        REQUIRE_ERROR(co_await task, asyncio::http::ws::CloseCode::NORMAL_CLOSURE);
    }
}

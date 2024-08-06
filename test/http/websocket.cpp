#include <asyncio/http/websocket.h>
#include <asyncio/net/stream.h>
#include <asyncio/buffer.h>
#include <zero/encoding/base64.h>
#include <catch2/catch_test_macros.hpp>
#include <openssl/sha.h>

constexpr auto PAYLOAD = "hello world";
constexpr auto URL = "http://localhost:30000/ws";
constexpr auto WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr auto MASKING_KEY_LENGTH = 4;

TEST_CASE("websocket", "[http]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        auto listener = asyncio::net::TCPListener::listen("127.0.0.1", 30000);
        REQUIRE(listener);

        SECTION("normal") {
            co_await allSettled(
                [](auto l) -> asyncio::task::Task<void> {
                    using namespace std::string_view_literals;

                    auto stream = co_await l.accept().transform([](asyncio::net::TCPStream &&rhs) {
                        return std::make_shared<asyncio::net::TCPStream>(std::move(rhs));
                    });
                    REQUIRE(stream);

                    asyncio::BufReader reader(*stream);

                    auto line = co_await reader.readLine();
                    REQUIRE(line);
                    REQUIRE(*line == "GET /ws HTTP/1.1");

                    std::map<std::string, std::string> headers;

                    while (true) {
                        line = co_await reader.readLine();
                        REQUIRE(line);

                        if (line->empty())
                            break;

                        const auto tokens = zero::strings::split(*line, ":", 1);
                        REQUIRE(tokens.size() == 2);
                        headers[tokens[0]] = zero::strings::trim(tokens[1]);
                    }

                    const auto it = headers.find("Sec-WebSocket-Key");
                    REQUIRE(it != headers.end());

                    std::array<std::byte, SHA_DIGEST_LENGTH> digest = {};
                    const std::string data = it->second + WS_MAGIC;

                    SHA1(
                        reinterpret_cast<const unsigned char *>(data.data()),
                        data.size(),
                        reinterpret_cast<unsigned char *>(digest.data())
                    );

                    const std::string response = fmt::format(
                        "HTTP/1.1 101 Switching Protocols\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        "Sec-WebSocket-Accept: {}\r\n\r\n",
                        zero::encoding::base64::encode(digest)
                    );

                    auto res = co_await stream.value()->writeAll(std::as_bytes(std::span{response}));
                    REQUIRE(res);

                    asyncio::http::ws::Header header;

                    res = co_await reader.readExactly({reinterpret_cast<std::byte *>(&header), sizeof(header)});
                    REQUIRE(res);
                    REQUIRE(header.opcode() == asyncio::http::ws::Opcode::TEXT);

                    std::array<std::byte, MASKING_KEY_LENGTH> key = {};
                    res = co_await reader.readExactly(key);
                    REQUIRE(res);

                    auto length = header.length();
                    std::vector<std::byte> payload(length);

                    res = co_await reader.readExactly(payload);
                    REQUIRE(res);

                    for (std::size_t i = 0; i < length; ++i)
                        payload[i] ^= key[i % 4];

                    REQUIRE(
                        std::string_view{reinterpret_cast<const char *>(payload.data()), payload.size()}
                        == PAYLOAD
                    );

                    header.mask(false);
                    header.opcode(asyncio::http::ws::Opcode::BINARY);

                    res = co_await stream.value()->writeAll({
                        reinterpret_cast<const std::byte *>(&header),
                        sizeof(header)
                    });
                    REQUIRE(res);

                    res = co_await stream.value()->writeAll(payload);
                    REQUIRE(res);

                    res = co_await reader.readExactly({reinterpret_cast<std::byte *>(&header), sizeof(header)});
                    REQUIRE(res);
                    REQUIRE(header.opcode() == asyncio::http::ws::Opcode::CLOSE);

                    res = co_await reader.readExactly(key);
                    REQUIRE(res);

                    length = header.length();
                    REQUIRE(length == 2);

                    payload.resize(length);
                    res = co_await reader.readExactly(payload);
                    REQUIRE(res);

                    for (std::size_t i = 0; i < length; ++i)
                        payload[i] ^= key[i % 4];

                    REQUIRE(
                        static_cast<asyncio::http::ws::CloseCode>(
                            ntohs(*reinterpret_cast<const unsigned short *>(payload.data()))
                        ) == asyncio::http::ws::CloseCode::NORMAL_CLOSURE
                    );

                    header.mask(false);
                    header.opcode(asyncio::http::ws::Opcode::CLOSE);

                    res = co_await stream.value()->writeAll({
                        reinterpret_cast<const std::byte *>(&header),
                        sizeof(header)
                    });
                    REQUIRE(res);

                    res = co_await stream.value()->writeAll(payload);
                    REQUIRE(res);
                }(*std::move(listener)),
                []() -> asyncio::task::Task<void> {
                    const auto url = asyncio::http::URL::from(URL);
                    REQUIRE(url);

                    auto ws = co_await asyncio::http::ws::WebSocket::connect(*url);
                    REQUIRE(ws);

                    auto res = co_await ws->sendText(PAYLOAD);
                    REQUIRE(res);

                    const auto message = co_await ws->readMessage();
                    REQUIRE(message);
                    REQUIRE(message->opcode == asyncio::http::ws::Opcode::BINARY);

                    const auto &payload = std::get<std::vector<std::byte>>(message->data);
                    REQUIRE(
                        std::string_view{reinterpret_cast<const char *>(payload.data()), payload.size()}
                        == PAYLOAD
                    );

                    res = co_await ws->close(asyncio::http::ws::CloseCode::NORMAL_CLOSURE);
                    REQUIRE(res);
                }()
            );
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

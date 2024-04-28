#include <asyncio/http/websocket.h>
#include <asyncio/net/ssl.h>
#include <asyncio/binary.h>
#include <zero/encoding/base64.h>
#include <zero/singleton.h>
#include <zero/expect.h>
#include <zero/defer.h>
#include <cassert>
#include <random>
#include <map>

#ifdef __ANDROID__
#include <endian.h>
#endif

constexpr auto SWITCHING_PROTOCOLS_STATUS = 101;
constexpr auto MASKING_KEY_LENGTH = 4;

constexpr auto TWO_BYTE_PAYLOAD_LENGTH = 126;
constexpr auto EIGHT_BYTE_PAYLOAD_LENGTH = 127;

constexpr auto MAX_SINGLE_BYTE_PAYLOAD_LENGTH = 125;
constexpr auto MAX_TWO_BYTE_PAYLOAD_LENGTH = (std::numeric_limits<std::uint16_t>::max)();

constexpr auto OPCODE_MASK = std::byte{0x0f};
constexpr auto FINAL_BIT = std::byte{0x80};
constexpr auto LENGTH_MASK = std::byte{0x7f};
constexpr auto MASK_BIT = std::byte{0x80};

constexpr auto WS_SCHEME = "http";
constexpr auto WS_SECURE_SCHEME = "https";
constexpr auto WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

asyncio::http::ws::Opcode asyncio::http::ws::Header::opcode() const {
    return static_cast<Opcode>(mBytes[0] & OPCODE_MASK);
}

bool asyncio::http::ws::Header::final() const {
    return std::to_integer<int>(mBytes[0] & FINAL_BIT);
}

std::size_t asyncio::http::ws::Header::length() const {
    return std::to_integer<int>(mBytes[1] & LENGTH_MASK);
}

bool asyncio::http::ws::Header::mask() const {
    return std::to_integer<int>(mBytes[1] & MASK_BIT);
}

void asyncio::http::ws::Header::opcode(const Opcode opcode) {
    mBytes[0] |= static_cast<std::byte>(opcode) & OPCODE_MASK;
}

void asyncio::http::ws::Header::final(const bool final) {
    if (!final) {
        mBytes[0] &= ~FINAL_BIT;
        return;
    }

    mBytes[0] |= FINAL_BIT;
}

void asyncio::http::ws::Header::length(const std::size_t length) {
    mBytes[1] |= static_cast<std::byte>(length) & LENGTH_MASK;
}

void asyncio::http::ws::Header::mask(const bool mask) {
    if (!mask) {
        mBytes[1] &= ~MASK_BIT;
        return;
    }

    mBytes[1] |= MASK_BIT;
}

const char *asyncio::http::ws::WebSocket::ErrorCategory::name() const noexcept {
    return "asyncio::http::ws::WebSocket";
}

std::string asyncio::http::ws::WebSocket::ErrorCategory::message(const int value) const {
    std::string msg;

    switch (value) {
    case UNSUPPORTED_MASKED_FRAME:
        msg = "unsupported masked frame";
        break;

    case UNSUPPORTED_OPCODE:
        msg = "unsupported opcode";
        break;

    case NOT_CONNECTED:
        msg = "websocket not connected";
        break;

    default:
        msg = "unknown";
        break;
    }

    return msg;
}

const char *asyncio::http::ws::WebSocket::CloseCodeCategory::name() const noexcept {
    return "asyncio::http::ws::WebSocket::close";
}

std::string asyncio::http::ws::WebSocket::CloseCodeCategory::message(const int value) const {
    std::string msg;

    switch (value) {
    case NORMAL_CLOSURE:
        msg = "normal closure";
        break;

    case GOING_AWAY:
        msg = "going away";
        break;

    case PROTOCOL_ERROR:
        msg = "protocol error";
        break;

    case UNSUPPORTED_DATA:
        msg = "unsupported data";
        break;

    case NO_STATUS_RCVD:
        msg = "no status rcvd";
        break;

    case ABNORMAL_CLOSURE:
        msg = "abnormal closure";
        break;

    case INVALID_TEXT:
        msg = "invalid text";
        break;

    case POLICY_VIOLATION:
        msg = "policy violation";
        break;

    case MESSAGE_TOO_BIG:
        msg = "message too big";
        break;

    case MANDATORY_EXTENSION:
        msg = "mandatory extension";
        break;

    case INTERNAL_ERROR:
        msg = "internal error";
        break;

    case SERVICE_RESTART:
        msg = "service restart";
        break;

    case TRY_AGAIN_LATER:
        msg = "try again later";
        break;

    case BAD_GATEWAY:
        msg = "bad gateway";
        break;

    default:
        msg = fmt::format("close code {}", value);
        break;
    }

    return msg;
}

asyncio::http::ws::WebSocket::WebSocket(std::unique_ptr<IBuffer> buffer)
    : mState(CONNECTED), mMutex(std::make_unique<sync::Mutex>()), mBuffer(std::move(buffer)) {
}

zero::async::coroutine::Task<asyncio::http::ws::Frame, std::error_code>
asyncio::http::ws::WebSocket::readFrame() const {
    Header header;

    CO_EXPECT(co_await mBuffer->readExactly({reinterpret_cast<std::byte *>(&header), sizeof(Header)}));

    if (header.mask())
        co_return tl::unexpected(UNSUPPORTED_MASKED_FRAME);

    std::vector<std::byte> data;

    if (const auto length = header.length(); length == EIGHT_BYTE_PAYLOAD_LENGTH) {
        const auto n = co_await binary::readBE<std::uint64_t>(*mBuffer);
        CO_EXPECT(n);
        data.resize(*n);
    }
    else if (length == TWO_BYTE_PAYLOAD_LENGTH) {
        const auto n = co_await binary::readBE<std::uint16_t>(*mBuffer);
        CO_EXPECT(n);
        data.resize(*n);
    }
    else {
        data.resize(length);
    }

    CO_EXPECT(co_await mBuffer->readExactly(data));
    co_return Frame{header, std::move(data)};
}

zero::async::coroutine::Task<asyncio::http::ws::InternalMessage, std::error_code>
asyncio::http::ws::WebSocket::readInternalMessage() const {
    auto frame = co_await readFrame();
    CO_EXPECT(frame);

    if (frame->header.final())
        co_return InternalMessage{frame->header.opcode(), std::move(frame->data)};

    while (true) {
        auto fragment = co_await readFrame();
        CO_EXPECT(fragment);

        std::ranges::copy(fragment->data, std::back_inserter(frame->data));

        if (fragment->header.final())
            break;
    }

    co_return InternalMessage{frame->header.opcode(), std::move(frame->data)};
}

zero::async::coroutine::Task<void, std::error_code>
asyncio::http::ws::WebSocket::writeInternalMessage(InternalMessage message) const {
    CO_EXPECT(co_await mMutex->lock());
    DEFER(mMutex->unlock());

    Header header;

    header.opcode(message.opcode);
    header.final(true);
    header.mask(true);

    std::size_t extendedBytes = 0;
    const std::size_t length = message.data.size();

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

    CO_EXPECT(co_await mBuffer->writeAll({reinterpret_cast<const std::byte *>(&header), sizeof(Header)}));

    if (extendedBytes == 2) {
        CO_EXPECT(co_await binary::writeBE(*mBuffer, static_cast<std::uint16_t>(length)));
    }
    else if (extendedBytes == 8) {
        CO_EXPECT(co_await binary::writeBE<std::uint64_t>(*mBuffer, length));
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution dist(0, 255);

    std::byte key[MASKING_KEY_LENGTH];

    for (auto &b: key)
        b = static_cast<std::byte>(dist(gen));

    CO_EXPECT(co_await mBuffer->writeAll(key));

    for (std::size_t i = 0; i < length; i++) {
        message.data[i] ^= key[i % 4];
    }

    co_return co_await mBuffer->writeAll(message.data);
}

zero::async::coroutine::Task<asyncio::http::ws::Message, std::error_code>
asyncio::http::ws::WebSocket::readMessage() const {
    while (true) {
        auto message = co_await readInternalMessage();
        CO_EXPECT(message);

        if (const auto opcode = message->opcode; opcode == TEXT) {
            co_return Message{
                message->opcode,
                std::string{reinterpret_cast<const char *>(message->data.data()), message->data.size()}
            };
        }
        else if (opcode == BINARY) {
            co_return Message{message->opcode, std::move(message->data)};
        }
        else if (opcode == PING) {
            CO_EXPECT(co_await writeInternalMessage({PONG, std::move(message->data)}));
        }
        else if (opcode == CLOSE) {
            CO_EXPECT(co_await writeInternalMessage({CLOSE, message->data}));
            CO_EXPECT(co_await mBuffer->close());

            if (message->data.size() < 2)
                co_return tl::unexpected(NO_STATUS_RCVD);

            co_return tl::unexpected(
                static_cast<CloseCode>(ntohs(*reinterpret_cast<const unsigned short *>(message->data.data())))
            );
        }
        else {
            co_return tl::unexpected(UNSUPPORTED_OPCODE);
        }
    }
}

zero::async::coroutine::Task<void, std::error_code> asyncio::http::ws::WebSocket::writeMessage(Message message) const {
    assert(message.opcode == TEXT || message.opcode == BINARY);

    if (message.opcode == TEXT) {
        const auto &text = std::get<std::string>(message.data);
        co_return co_await writeInternalMessage({
                message.opcode,
                {
                    reinterpret_cast<const std::byte *>(text.data()),
                    reinterpret_cast<const std::byte *>(text.data()) + text.size()
                }
            }
        );
    }

    co_return co_await writeInternalMessage({
        message.opcode,
        std::move(std::get<std::vector<std::byte>>(message.data))
    });
}

zero::async::coroutine::Task<void, std::error_code> asyncio::http::ws::WebSocket::sendText(std::string text) const {
    co_return co_await writeMessage({TEXT, std::move(text)});
}

zero::async::coroutine::Task<void, std::error_code>
asyncio::http::ws::WebSocket::sendBinary(const std::span<const std::byte> data) const {
    co_return co_await writeMessage({BINARY, std::vector<std::byte>{data.begin(), data.end()}});
}

zero::async::coroutine::Task<void, std::error_code> asyncio::http::ws::WebSocket::close(const CloseCode code) {
    if (mState != CONNECTED)
        co_return tl::unexpected(NOT_CONNECTED);

    mState = CLOSING;
    const auto c = htons(code);

    CO_EXPECT(co_await writeInternalMessage({
        CLOSE,
        {reinterpret_cast<const std::byte *>(&c), reinterpret_cast<const std::byte *>(&c) + sizeof(c)}}
    ));

    while (true) {
        const auto message = co_await readInternalMessage();
        CO_EXPECT(message);

        if (message->opcode == CLOSE) {
            mState = CLOSED;
            break;
        }
    }

    CO_EXPECT(co_await mBuffer->close());
    co_return tl::expected<void, std::error_code>{};
}

std::error_code asyncio::http::ws::make_error_code(const WebSocket::Error e) {
    return {static_cast<int>(e), zero::Singleton<WebSocket::ErrorCategory>::getInstance()};
}

std::error_code asyncio::http::ws::make_error_code(const WebSocket::CloseCode e) {
    return {static_cast<int>(e), zero::Singleton<WebSocket::CloseCodeCategory>::getInstance()};
}

const char *asyncio::http::ws::HandshakeErrorCategory::name() const noexcept {
    return "asyncio::http::ws::handshake";
}

std::string asyncio::http::ws::HandshakeErrorCategory::message(const int value) const {
    std::string msg;

    switch (value) {
    case UNSUPPORTED_SCHEME:
        msg = "unsupported websocket scheme";
        break;

    case INVALID_RESPONSE:
        msg = "invalid http response";
        break;

    case UNEXPECTED_STATUS_CODE:
        msg = "unexpected http response status code";
        break;

    case INVALID_HTTP_HEADER:
        msg = "invalid http header";
        break;

    case NO_ACCEPT_HEADER:
        msg = "no websocket accept header";
        break;

    case HASH_MISMATCH:
        msg = "hash mismatch";
        break;

    default:
        msg = "unknown";
        break;
    }

    return msg;
}

std::error_code asyncio::http::ws::make_error_code(const HandshakeError e) {
    return {e, zero::Singleton<HandshakeErrorCategory>::getInstance()};
}

zero::async::coroutine::Task<asyncio::http::ws::WebSocket, std::error_code> asyncio::http::ws::connect(const URL url) {
    const auto scheme = url.scheme();
    const auto host = url.host();
    const auto port = url.port();

    CO_EXPECT(scheme);
    CO_EXPECT(host);
    CO_EXPECT(port);

    std::unique_ptr<IBuffer> buffer;

    if (*scheme == WS_SCHEME) {
        auto buf = co_await net::stream::connect(*host, *port).transform([](net::stream::Buffer &&b) {
            return std::make_unique<net::stream::Buffer>(std::move(b));
        });
        CO_EXPECT(buf);
        buffer = *std::move(buf);
    }
    else if (*scheme == WS_SECURE_SCHEME) {
        auto buf = co_await net::ssl::stream::connect(*host, *port).transform([](net::ssl::stream::Buffer &&b) {
            return std::make_unique<net::ssl::stream::Buffer>(std::move(b));
        });
        CO_EXPECT(buf);
        buffer = *std::move(buf);
    }
    else {
        co_return tl::unexpected(UNSUPPORTED_SCHEME);
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution dist(0, 255);

    std::byte secret[16];

    for (auto &b: secret)
        b = static_cast<std::byte>(dist(gen));

    const std::string key = zero::encoding::base64::encode(secret);
    const auto path = url.path().value_or("/");
    const auto query = url.query();

    const auto header = fmt::format(
        "GET {} HTTP/1.1\r\n"
        "Host: {}:{}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: upgrade\r\n"
        "Sec-WebSocket-Key: {}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Origin: {}://{}:{}\r\n\r\n",
        !query ? path : path + "?" + *query,
        *host, *port,
        key,
        *scheme, *host, *port
    );

    CO_EXPECT(co_await buffer->writeAll(std::as_bytes(std::span{header})));

    auto line = co_await buffer->readLine();
    CO_EXPECT(line);

    auto tokens = zero::strings::split(*line);

    if (tokens.size() < 2)
        co_return tl::unexpected(INVALID_RESPONSE);

    const auto code = zero::strings::toNumber<int>(tokens[1]);
    CO_EXPECT(code);

    if (code != SWITCHING_PROTOCOLS_STATUS)
        co_return tl::unexpected(UNEXPECTED_STATUS_CODE);

    std::map<std::string, std::string> headers;

    while (true) {
        line = co_await buffer->readLine();
        CO_EXPECT(line);

        if (line->empty())
            break;

        tokens = zero::strings::split(*line, ":", 1);

        if (tokens.size() != 2)
            co_return tl::unexpected(INVALID_HTTP_HEADER);

        headers[tokens[0]] = zero::strings::trim(tokens[1]);
    }

    const auto it = headers.find("Sec-WebSocket-Accept");

    if (it == headers.end())
        co_return tl::unexpected(NO_ACCEPT_HEADER);

    std::byte digest[SHA_DIGEST_LENGTH];
    const std::string data = key + WS_MAGIC;

    SHA1(reinterpret_cast<const unsigned char *>(data.data()), data.size(), reinterpret_cast<unsigned char *>(digest));

    if (it->second != zero::encoding::base64::encode(digest))
        co_return tl::unexpected(HASH_MISMATCH);

    co_return WebSocket{std::move(buffer)};
}

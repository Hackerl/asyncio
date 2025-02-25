#include <asyncio/http/websocket.h>
#include <asyncio/net/stream.h>
#include <asyncio/net/tls.h>
#include <asyncio/binary.h>
#include <asyncio/buffer.h>
#include <zero/encoding/base64.h>
#include <zero/defer.h>
#include <openssl/sha.h>
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
    mBytes[0] &= ~OPCODE_MASK;
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
    mBytes[1] &= ~LENGTH_MASK;
    mBytes[1] |= static_cast<std::byte>(length) & LENGTH_MASK;
}

void asyncio::http::ws::Header::mask(const bool mask) {
    if (!mask) {
        mBytes[1] &= ~MASK_BIT;
        return;
    }

    mBytes[1] |= MASK_BIT;
}

asyncio::task::Task<asyncio::http::ws::WebSocket, std::error_code> asyncio::http::ws::WebSocket::connect(const URL url) {
    const auto scheme = url.scheme();
    const auto host = url.host();
    const auto port = url.port();

    if (!host || !port)
        co_return std::unexpected{Error::INVALID_URL};

    std::shared_ptr<IReader> reader;
    std::shared_ptr<IWriter> writer;
    std::shared_ptr<ICloseable> closeable;

    if (scheme == WS_SCHEME) {
        auto stream = co_await net::TCPStream::connect(*host, *port)
            .transform([](net::TCPStream &&value) {
                return std::make_shared<net::TCPStream>(std::move(value));
            });
        CO_EXPECT(stream);
        reader = *stream;
        writer = *stream;
        closeable = *std::move(stream);
    }
    else if (scheme == WS_SECURE_SCHEME) {
        auto stream = co_await net::TCPStream::connect(*host, *port);
        CO_EXPECT(stream);

        auto context = net::tls::ClientConfig().build();
        CO_EXPECT(context);

        auto tls = co_await net::tls::connect(*std::move(stream), *std::move(context), *host)
            .transform([](net::tls::TLS<net::TCPStream> &&value) {
                return std::make_shared<net::tls::TLS<net::TCPStream>>(std::move(value));
            });
        CO_EXPECT(tls);
        reader = *tls;
        writer = *tls;
        closeable = *std::move(tls);
    }
    else {
        co_return std::unexpected{Error::UNSUPPORTED_SCHEME};
    }

    BufReader bufReader{reader};

    std::random_device rd;
    std::mt19937 gen{rd()};
    std::uniform_int_distribution dist{0, 255};

    std::array<std::byte, 16> secret{};
    std::ranges::generate(secret, [&] { return static_cast<std::byte>(dist(gen)); });

    const auto key = zero::encoding::base64::encode(secret);
    const auto path = url.path();
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
        scheme, *host, *port
    );

    CO_EXPECT(co_await writer->writeAll(std::as_bytes(std::span{header})));

    auto line = co_await bufReader.readLine();
    CO_EXPECT(line);

    auto tokens = zero::strings::split(*line);

    if (tokens.size() < 2)
        co_return std::unexpected{Error::INVALID_RESPONSE};

    const auto code = zero::strings::toNumber<int>(tokens[1]);
    CO_EXPECT(code);

    if (code != SWITCHING_PROTOCOLS_STATUS)
        co_return std::unexpected{Error::UNEXPECTED_STATUS_CODE};

    std::map<std::string, std::string> headers;

    while (true) {
        line = co_await bufReader.readLine();
        CO_EXPECT(line);

        if (line->empty())
            break;

        tokens = zero::strings::split(*line, ":", 1);

        if (tokens.size() != 2)
            co_return std::unexpected{Error::INVALID_HTTP_HEADER};

        headers[tokens[0]] = zero::strings::trim(tokens[1]);
    }

    const auto it = headers.find("Sec-WebSocket-Accept");

    if (it == headers.end())
        co_return std::unexpected{Error::NO_ACCEPT_HEADER};

    std::array<std::byte, SHA_DIGEST_LENGTH> digest{};
    const auto data = key + WS_MAGIC;

    SHA1(
        reinterpret_cast<const unsigned char *>(data.data()),
        data.size(),
        reinterpret_cast<unsigned char *>(digest.data())
    );

    if (it->second != zero::encoding::base64::encode(digest))
        co_return std::unexpected{Error::HASH_MISMATCH};

    co_return WebSocket{
        std::make_shared<BufReader<std::shared_ptr<IReader>>>(std::move(bufReader)),
        std::move(writer),
        std::move(closeable)
    };
}

asyncio::http::ws::WebSocket::WebSocket(
    std::shared_ptr<IReader> reader,
    std::shared_ptr<IWriter> writer,
    std::shared_ptr<ICloseable> closeable
) : mState{State::CONNECTED}, mMutex{std::make_unique<sync::Mutex>()},
    mReader{std::move(reader)}, mWriter{std::move(writer)}, mCloseable{std::move(closeable)} {
}

asyncio::task::Task<asyncio::http::ws::Frame, std::error_code>
asyncio::http::ws::WebSocket::readFrame() {
    Header header;

    CO_EXPECT(co_await mReader->readExactly({reinterpret_cast<std::byte *>(&header), sizeof(Header)}));

    if (header.mask())
        co_return std::unexpected{Error::UNSUPPORTED_MASKED_FRAME};

    std::vector<std::byte> data;

    if (const auto length = header.length(); length == EIGHT_BYTE_PAYLOAD_LENGTH) {
        const auto n = co_await binary::readBE<std::uint64_t>(mReader);
        CO_EXPECT(n);
        data.resize(*n);
    }
    else if (length == TWO_BYTE_PAYLOAD_LENGTH) {
        const auto n = co_await binary::readBE<std::uint16_t>(mReader);
        CO_EXPECT(n);
        data.resize(*n);
    }
    else {
        data.resize(length);
    }

    CO_EXPECT(co_await mReader->readExactly(data));
    co_return Frame{header, std::move(data)};
}

asyncio::task::Task<asyncio::http::ws::InternalMessage, std::error_code>
asyncio::http::ws::WebSocket::readInternalMessage() {
    assert(mState != State::CLOSED);

    auto frame = co_await readFrame();
    CO_EXPECT(frame);

    if (frame->header.final())
        co_return InternalMessage{frame->header.opcode(), std::move(frame->data)};

    while (true) {
        auto fragment = co_await readFrame();
        CO_EXPECT(fragment);

        frame->data.insert(frame->data.end(), fragment->data.begin(), fragment->data.end());

        if (fragment->header.final())
            break;
    }

    co_return InternalMessage{frame->header.opcode(), std::move(frame->data)};
}

asyncio::task::Task<void, std::error_code>
asyncio::http::ws::WebSocket::writeInternalMessage(InternalMessage message) {
    CO_EXPECT(co_await mMutex->lock());
    DEFER(mMutex->unlock());
    assert(mState != State::CLOSED);

    Header header;

    header.opcode(message.opcode);
    header.final(true);
    header.mask(true);

    std::size_t extendedBytes{};
    const auto length = message.data.size();

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

    CO_EXPECT(co_await mWriter->writeAll({reinterpret_cast<const std::byte *>(&header), sizeof(Header)}));

    if (extendedBytes == 2) {
        CO_EXPECT(co_await binary::writeBE(mWriter, static_cast<std::uint16_t>(length)));
    }
    else if (extendedBytes == 8) {
        CO_EXPECT(co_await binary::writeBE<std::uint64_t>(mWriter, length));
    }

    std::random_device rd;
    std::mt19937 gen{rd()};
    std::uniform_int_distribution dist{0, 255};

    std::array<std::byte, MASKING_KEY_LENGTH> key{};
    std::ranges::generate(key, [&] { return static_cast<std::byte>(dist(gen)); });

    CO_EXPECT(co_await mWriter->writeAll(key));

    for (std::size_t i{0}; i < length; ++i)
        message.data[i] ^= key[i % 4];

    co_return co_await mWriter->writeAll(message.data);
}

asyncio::task::Task<asyncio::http::ws::Message, std::error_code>
asyncio::http::ws::WebSocket::readMessage() {
    assert(mState == State::CONNECTED);

    while (true) {
        auto message = co_await readInternalMessage();
        CO_EXPECT(message);

        if (const auto opcode = message->opcode; opcode == Opcode::TEXT) {
            co_return Message{
                message->opcode,
                std::string{reinterpret_cast<const char *>(message->data.data()), message->data.size()}
            };
        }
        else if (opcode == Opcode::BINARY) {
            co_return Message{message->opcode, std::move(message->data)};
        }
        else if (opcode == Opcode::PING) {
            CO_EXPECT(co_await writeInternalMessage({Opcode::PONG, std::move(message->data)}));
        }
        else if (opcode == Opcode::CLOSE) {
            mState = State::CLOSING;
            CO_EXPECT(co_await writeInternalMessage({Opcode::CLOSE, message->data}));
            mState = State::CLOSED;

            if (message->data.size() < 2)
                co_return std::unexpected{CloseCode::NORMAL_CLOSURE};

            co_return std::unexpected{
                static_cast<CloseCode>(ntohs(*reinterpret_cast<const std::uint16_t *>(message->data.data())))
            };
        }
        else {
            co_return std::unexpected{Error::UNSUPPORTED_OPCODE};
        }
    }
}

asyncio::task::Task<void, std::error_code> asyncio::http::ws::WebSocket::writeMessage(Message message) {
    assert(message.opcode == Opcode::TEXT || message.opcode == Opcode::BINARY);

    if (mState != State::CONNECTED)
        co_return std::unexpected{Error::CONNECTION_CLOSED};

    if (message.opcode == Opcode::TEXT) {
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

asyncio::task::Task<void, std::error_code> asyncio::http::ws::WebSocket::sendText(std::string text) {
    co_return co_await writeMessage({Opcode::TEXT, std::move(text)});
}

asyncio::task::Task<void, std::error_code>
asyncio::http::ws::WebSocket::sendBinary(const std::span<const std::byte> data) {
    co_return co_await writeMessage({Opcode::BINARY, std::vector<std::byte>{data.begin(), data.end()}});
}

asyncio::task::Task<void, std::error_code> asyncio::http::ws::WebSocket::close(const CloseCode code) {
    if (mState == State::CLOSED)
        co_return co_await mCloseable->close();

    assert(mState == State::CONNECTED);

    mState = State::CLOSING;
    const auto c = htons(static_cast<std::uint16_t>(code));

    CO_EXPECT(co_await writeInternalMessage({
        Opcode::CLOSE,
        {reinterpret_cast<const std::byte *>(&c), reinterpret_cast<const std::byte *>(&c) + sizeof(c)}}
    ));

    while (true) {
        const auto message = co_await readInternalMessage();
        CO_EXPECT(message);

        if (message->opcode == Opcode::CLOSE) {
            mState = State::CLOSED;
            break;
        }
    }

    CO_EXPECT(co_await mCloseable->close());
    co_return {};
}

DEFINE_ERROR_CATEGORY_INSTANCES(asyncio::http::ws::WebSocket::Error, asyncio::http::ws::CloseCode)

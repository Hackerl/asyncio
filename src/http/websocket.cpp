#include <asyncio/http/websocket.h>
#include <asyncio/net/stream.h>
#include <asyncio/net/tls.h>
#include <asyncio/binary.h>
#include <asyncio/buffer.h>
#include <asyncio/thread.h>
#include <zero/defer.h>
#include <zero/utility.h>
#include <zero/encoding/base64.h>
#include <openssl/sha.h>
#include <random>
#include <map>

#ifdef __ANDROID__
#include <endian.h>
#endif

constexpr auto SwitchingProtocolsStatus = 101;
constexpr auto MaskingKeyLength = 4;

constexpr auto TwoBytePayloadLength = 126;
constexpr auto EightBytePayloadLength = 127;

constexpr auto MaxSingleBytePayloadLength = 125;
constexpr auto MaxTwoBytePayloadLength = (std::numeric_limits<std::uint16_t>::max)();

constexpr auto OpcodeMask = std::byte{0x0f};
constexpr auto RSV1Bit = std::byte{0x40};
constexpr auto FinalBit = std::byte{0x80};
constexpr auto LengthMask = std::byte{0x7f};
constexpr auto MaskBit = std::byte{0x80};

constexpr auto WebSocketScheme = "http";
constexpr auto WebSocketSecureScheme = "https";
constexpr auto WebSocketMagic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

constexpr auto WebSocketCompressionThreshold = 128;

Z_DEFINE_ERROR_TRANSFORMER(ZLIBError, "zlib", zError)
Z_DECLARE_ERROR_CODE(ZLIBError)

Z_DEFINE_ERROR_CATEGORY_INSTANCE(ZLIBError)

namespace {
    std::expected<void, std::error_code>
    validateWebSocketAccept(const std::map<std::string, std::string> &headers, const std::string &key) {
        const auto it = headers.find("Sec-WebSocket-Accept");

        if (it == headers.end())
            return std::unexpected{asyncio::http::ws::WebSocket::Error::NoAcceptHeader};

        std::array<std::byte, SHA_DIGEST_LENGTH> digest{};
        const auto data = key + WebSocketMagic;

        SHA1(
            reinterpret_cast<const unsigned char *>(data.data()),
            data.size(),
            reinterpret_cast<unsigned char *>(digest.data())
        );

        if (it->second != zero::encoding::base64::encode(digest))
            return std::unexpected{asyncio::http::ws::WebSocket::Error::HashMismatch};

        return {};
    }

    std::expected<std::optional<asyncio::http::ws::DeflateConfig>, std::error_code>
    parseExtensionConfig(const std::map<std::string, std::string> &headers) {
        const auto it = headers.find("Sec-WebSocket-Extensions");

        if (it == headers.end())
            return std::nullopt;

        const auto items = zero::strings::split(it->second, ';')
            | std::views::transform(zero::strings::trim)
            | std::ranges::to<std::vector>();

        if (std::ranges::find(items, "permessage-deflate") == items.end())
            return std::nullopt;

        asyncio::http::ws::DeflateConfig config;

        for (const auto &item: items) {
            if (item == "server_no_context_takeover") {
                config.serverNoContextTakeover = true;
            }
            else if (item.starts_with("client_max_window_bits=")) {
                const auto windowBits = zero::strings::toNumber<int>(item.substr(23));
                Z_EXPECT(windowBits);
                config.clientMaxWindowBits = *windowBits;
            }
        }

        return config;
    }
}

asyncio::http::ws::Opcode asyncio::http::ws::Header::opcode() const {
    return static_cast<Opcode>(mBytes[0] & OpcodeMask);
}

bool asyncio::http::ws::Header::rsv1() const {
    return std::to_integer<int>(mBytes[0] & RSV1Bit);
}

bool asyncio::http::ws::Header::final() const {
    return std::to_integer<int>(mBytes[0] & FinalBit);
}

std::size_t asyncio::http::ws::Header::length() const {
    return std::to_integer<int>(mBytes[1] & LengthMask);
}

bool asyncio::http::ws::Header::mask() const {
    return std::to_integer<int>(mBytes[1] & MaskBit);
}

void asyncio::http::ws::Header::opcode(const Opcode opcode) {
    mBytes[0] &= ~OpcodeMask;
    mBytes[0] |= static_cast<std::byte>(opcode) & OpcodeMask;
}

void asyncio::http::ws::Header::rsv1(const bool rsv1) {
    if (!rsv1) {
        mBytes[0] &= ~RSV1Bit;
        return;
    }

    mBytes[0] |= RSV1Bit;
}

void asyncio::http::ws::Header::final(const bool final) {
    if (!final) {
        mBytes[0] &= ~FinalBit;
        return;
    }

    mBytes[0] |= FinalBit;
}

void asyncio::http::ws::Header::length(const std::size_t length) {
    mBytes[1] &= ~LengthMask;
    mBytes[1] |= static_cast<std::byte>(length) & LengthMask;
}

void asyncio::http::ws::Header::mask(const bool mask) {
    if (!mask) {
        mBytes[1] &= ~MaskBit;
        return;
    }

    mBytes[1] |= MaskBit;
}

asyncio::http::ws::Compressor::Compressor(std::unique_ptr<z_stream, void(*)(z_stream *)> stream)
    : mStream{std::move(stream)} {
}

std::expected<asyncio::http::ws::Compressor, std::error_code>
asyncio::http::ws::Compressor::make(const int windowBits) {
    auto stream = std::make_unique<z_stream>();

    if (const auto result = deflateInit2(
        stream.get(),
        Z_DEFAULT_COMPRESSION,
        Z_DEFLATED,
        -windowBits,
        8,
        Z_DEFAULT_STRATEGY
    ); result != Z_OK)
        return std::unexpected{static_cast<ZLIBError>(result)};

    return Compressor{
        {
            stream.release(),
            [](auto *s) {
                deflateEnd(s);
                delete s;
            }
        }
    };
}

// ReSharper disable once CppMemberFunctionMayBeConst
asyncio::task::Task<std::vector<std::byte>, std::error_code>
asyncio::http::ws::Compressor::compress(const std::span<const std::byte> data) {
    co_return zero::flattenWith<std::error_code>(
        co_await toThreadPool([&]() -> std::expected<std::vector<std::byte>, std::error_code> {
            std::vector<std::byte> output;

            mStream->avail_in = data.size();
            mStream->next_in = reinterpret_cast<Bytef *>(const_cast<std::byte *>(data.data()));

            do {
                std::array<std::byte, 16384> buffer; // NOLINT(*-pro-type-member-init)

                mStream->avail_out = buffer.size();
                mStream->next_out = reinterpret_cast<Bytef *>(buffer.data());

                if (const auto result = deflate(mStream.get(), Z_SYNC_FLUSH); result != Z_OK && result != Z_BUF_ERROR)
                    return std::unexpected{static_cast<ZLIBError>(result)};

                output.append_range(std::span{buffer.data(), buffer.size() - mStream->avail_out});
            }
            while (mStream->avail_out == 0);
            assert(mStream->avail_in == 0);

            return output;
        })
    );
}

// ReSharper disable once CppMemberFunctionMayBeConst
std::expected<void, std::error_code> asyncio::http::ws::Compressor::reset() {
    if (const auto result = deflateReset(mStream.get()); result != Z_OK)
        return std::unexpected{static_cast<ZLIBError>(result)};

    return {};
}

asyncio::http::ws::Decompressor::Decompressor(std::unique_ptr<z_stream, void(*)(z_stream *)> stream)
    : mStream{std::move(stream)} {
}

std::expected<asyncio::http::ws::Decompressor, std::error_code>
asyncio::http::ws::Decompressor::make(const int windowBits) {
    auto stream = std::make_unique<z_stream>();

    if (const auto result = inflateInit2(stream.get(), -windowBits); result != Z_OK)
        return std::unexpected{static_cast<ZLIBError>(result)};

    return Decompressor{
        {
            stream.release(),
            [](auto *s) {
                inflateEnd(s);
                delete s;
            }
        }
    };
}

// ReSharper disable once CppMemberFunctionMayBeConst
asyncio::task::Task<std::vector<std::byte>, std::error_code>
asyncio::http::ws::Decompressor::decompress(const std::span<const std::byte> data) {
    co_return zero::flattenWith<std::error_code>(
        co_await toThreadPool([&]() -> std::expected<std::vector<std::byte>, std::error_code> {
            std::vector<std::byte> output;
            output.reserve(data.size() * 2);

            mStream->avail_in = data.size();
            mStream->next_in = reinterpret_cast<Bytef *>(const_cast<std::byte *>(data.data()));

            do {
                std::array<std::byte, 16384> buffer; // NOLINT(*-pro-type-member-init)

                mStream->avail_out = buffer.size();
                mStream->next_out = reinterpret_cast<Bytef *>(buffer.data());

                if (const auto result = inflate(mStream.get(), Z_SYNC_FLUSH);
                    result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR)
                    return std::unexpected{static_cast<ZLIBError>(result)};

                output.append_range(std::span{buffer.data(), buffer.size() - mStream->avail_out});
            }
            while (mStream->avail_out == 0);

            return output;
        })
    );
}

// ReSharper disable once CppMemberFunctionMayBeConst
std::expected<void, std::error_code> asyncio::http::ws::Decompressor::reset() {
    if (const auto result = inflateReset(mStream.get()); result != Z_OK)
        return std::unexpected{static_cast<ZLIBError>(result)};

    return {};
}

asyncio::http::ws::WebSocket::WebSocket(
    std::shared_ptr<IReader> reader,
    std::shared_ptr<IWriter> writer,
    std::shared_ptr<ICloseable> closeable,
    std::optional<DeflateExtension> deflateExtension
) : mState{State::Connected}, mMutex{std::make_unique<sync::Mutex>()},
    mReader{std::move(reader)}, mWriter{std::move(writer)}, mCloseable{std::move(closeable)},
    mDeflateExtension{std::move(deflateExtension)} {
}

asyncio::task::Task<asyncio::http::ws::WebSocket, std::error_code>
asyncio::http::ws::WebSocket::connect(const URL url, std::optional<net::tls::Context> context) {
    const auto scheme = url.scheme();
    const auto host = url.host();
    const auto port = url.port();

    if (!host || !port)
        co_return std::unexpected{Error::InvalidURL};

    std::shared_ptr<IReader> reader;
    std::shared_ptr<IWriter> writer;
    std::shared_ptr<ICloseable> closeable;

    if (scheme == WebSocketScheme) {
        auto stream = co_await net::TCPStream::connect(*host, *port)
            .transform([](net::TCPStream &&value) {
                return std::make_shared<net::TCPStream>(std::move(value));
            });
        Z_CO_EXPECT(stream);
        reader = *stream;
        writer = *stream;
        closeable = *std::move(stream);
    }
    else if (scheme == WebSocketSecureScheme) {
        auto stream = co_await net::TCPStream::connect(*host, *port);
        Z_CO_EXPECT(stream);

        if (!context) {
            auto ctx = net::tls::ClientConfig().build();
            Z_CO_EXPECT(ctx);
            context = *std::move(ctx);
        }

        auto tls = co_await net::tls::connect(*std::move(stream), *std::move(context), *host)
            .transform([](net::tls::TLS<net::TCPStream> &&value) {
                return std::make_shared<net::tls::TLS<net::TCPStream>>(std::move(value));
            });
        Z_CO_EXPECT(tls);
        reader = *tls;
        writer = *tls;
        closeable = *std::move(tls);
    }
    else {
        co_return std::unexpected{Error::UnsupportedScheme};
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
        "Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n"
        "Origin: {}://{}:{}\r\n\r\n",
        !query ? path : path + "?" + *query,
        *host, *port,
        key,
        scheme, *host, *port
    );

    Z_CO_EXPECT(co_await writer->writeAll(std::as_bytes(std::span{header})));

    {
        const auto line = co_await bufReader.readLine();
        Z_CO_EXPECT(line);

        const auto tokens = zero::strings::split(*line);

        if (tokens.size() < 2)
            co_return std::unexpected{Error::InvalidResponse};

        const auto code = zero::strings::toNumber<int>(tokens[1]);
        Z_CO_EXPECT(code);

        if (code != SwitchingProtocolsStatus)
            co_return std::unexpected{Error::UnexpectedStatusCode};
    }

    std::map<std::string, std::string> headers;

    while (true) {
        const auto line = co_await bufReader.readLine();
        Z_CO_EXPECT(line);

        if (line->empty())
            break;

        const auto tokens = zero::strings::split(*line, ":", 1);

        if (tokens.size() != 2)
            co_return std::unexpected{Error::InvalidHTTPHeader};

        headers[tokens[0]] = zero::strings::trim(tokens[1]);
    }

    Z_CO_EXPECT(validateWebSocketAccept(headers, key));

    const auto config = parseExtensionConfig(headers);
    Z_CO_EXPECT(config);

    std::optional<DeflateExtension> deflateExtension;

    if (const auto &deflateConfig = *config) {
        auto compressor = Compressor::make(deflateConfig->clientMaxWindowBits);
        Z_CO_EXPECT(compressor);

        auto decompressor = Decompressor::make(deflateConfig->serverMaxWindowBits);
        Z_CO_EXPECT(decompressor);

        deflateExtension.emplace(*deflateConfig, *std::move(compressor), *std::move(decompressor));
    }

    co_return WebSocket{
        std::make_shared<BufReader<std::shared_ptr<IReader>>>(std::move(bufReader)),
        std::move(writer),
        std::move(closeable),
        std::move(deflateExtension)
    };
}

asyncio::task::Task<asyncio::http::ws::Frame, std::error_code>
asyncio::http::ws::WebSocket::readFrame() {
    Header header;

    Z_CO_EXPECT(co_await mReader->readExactly({reinterpret_cast<std::byte *>(&header), sizeof(Header)}));

    if (header.mask())
        co_return std::unexpected{Error::UnsupportedMaskedFrame};

    std::vector<std::byte> data;

    if (const auto length = header.length(); length == EightBytePayloadLength) {
        const auto n = co_await binary::readBE<std::uint64_t>(mReader);
        Z_CO_EXPECT(n);
        data.resize(*n);
    }
    else if (length == TwoBytePayloadLength) {
        const auto n = co_await binary::readBE<std::uint16_t>(mReader);
        Z_CO_EXPECT(n);
        data.resize(*n);
    }
    else {
        data.resize(length);
    }

    Z_CO_EXPECT(co_await mReader->readExactly(data));
    co_return Frame{header, std::move(data)};
}

asyncio::task::Task<asyncio::http::ws::InternalMessage, std::error_code>
asyncio::http::ws::WebSocket::readInternalMessage() {
    assert(mState != State::Closed);

    auto frame = co_await readFrame();
    Z_CO_EXPECT(frame);

    auto final = frame->header.final();

    while (!final) {
        const auto fragment = co_await readFrame();
        Z_CO_EXPECT(fragment);

        frame->data.append_range(fragment->data);
        final = fragment->header.final();
    }

    if (frame->header.rsv1()) {
        if (!mDeflateExtension)
            co_return std::unexpected{Error::UnexpectedCompressedMessage};

        frame->data.append_range(std::array{std::byte{0x00}, std::byte{0x00}, std::byte{0xff}, std::byte{0xff}});

        auto &[config, compressor, decompressor] = *mDeflateExtension;
        auto decompressed = co_await decompressor.decompress(frame->data);
        Z_CO_EXPECT(decompressed);

        frame->data = *std::move(decompressed);

        if (config.serverNoContextTakeover) {
            Z_CO_EXPECT(decompressor.reset());
        }
    }

    co_return InternalMessage{frame->header.opcode(), std::move(frame->data)};
}

asyncio::task::Task<void, std::error_code>
asyncio::http::ws::WebSocket::writeInternalMessage(InternalMessage message) {
    Z_CO_EXPECT(co_await mMutex->lock());
    Z_DEFER(mMutex->unlock());

    if (mState == State::Closed || (mState == State::Closing && message.opcode != Opcode::Close))
        co_return std::unexpected{Error::ConnectionClosed};

    Header header;

    header.opcode(message.opcode);
    header.final(true);
    header.mask(true);

    if (mDeflateExtension && (message.opcode == Opcode::Text || message.opcode == Opcode::Binary)
        && message.data.size() >= WebSocketCompressionThreshold) {
        auto compressed = co_await mDeflateExtension->compressor.compress(message.data);
        Z_CO_EXPECT(compressed);

        assert(compressed->size() > 4);
        compressed->resize(compressed->size() - 4);

        message.data = *std::move(compressed);
        header.rsv1(true);
    }

    std::size_t extendedBytes{};
    const auto length = message.data.size();

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

    Z_CO_EXPECT(co_await mWriter->writeAll({reinterpret_cast<const std::byte *>(&header), sizeof(Header)}));

    if (extendedBytes == 2) {
        Z_CO_EXPECT(co_await binary::writeBE(mWriter, static_cast<std::uint16_t>(length)));
    }
    else if (extendedBytes == 8) {
        Z_CO_EXPECT(co_await binary::writeBE<std::uint64_t>(mWriter, length));
    }

    std::random_device rd;
    std::mt19937 gen{rd()};
    std::uniform_int_distribution dist{0, 255};

    std::array<std::byte, MaskingKeyLength> key{};
    std::ranges::generate(key, [&] { return static_cast<std::byte>(dist(gen)); });

    Z_CO_EXPECT(co_await mWriter->writeAll(key));

    for (std::size_t i{0}; i < length; ++i)
        message.data[i] ^= key[i % 4];

    co_return co_await mWriter->writeAll(message.data);
}

asyncio::task::Task<asyncio::http::ws::Message, std::error_code>
asyncio::http::ws::WebSocket::readMessage() {
    assert(mState == State::Connected);

    while (true) {
        auto message = co_await readInternalMessage();
        Z_CO_EXPECT(message);

        if (const auto opcode = message->opcode; opcode == Opcode::Text) {
            co_return Message{
                message->opcode,
                std::string{reinterpret_cast<const char *>(message->data.data()), message->data.size()}
            };
        }
        else if (opcode == Opcode::Binary) {
            co_return Message{message->opcode, std::move(message->data)};
        }
        else if (opcode == Opcode::Ping) {
            Z_CO_EXPECT(co_await writeInternalMessage({Opcode::Pong, std::move(message->data)}));
        }
        else if (opcode == Opcode::Close) {
            mState = State::Closing;
            Z_CO_EXPECT(co_await writeInternalMessage({Opcode::Close, message->data}));
            mState = State::Closed;

            if (message->data.size() < 2)
                co_return std::unexpected{CloseCode::NormalClosure};

            co_return std::unexpected{
                static_cast<CloseCode>(ntohs(*reinterpret_cast<const std::uint16_t *>(message->data.data())))
            };
        }
        else {
            co_return std::unexpected{Error::UnsupportedOpcode};
        }
    }
}

asyncio::task::Task<void, std::error_code> asyncio::http::ws::WebSocket::writeMessage(Message message) {
    assert(message.opcode == Opcode::Text || message.opcode == Opcode::Binary);

    if (message.opcode == Opcode::Text) {
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
    return writeMessage({Opcode::Text, std::move(text)});
}

asyncio::task::Task<void, std::error_code>
asyncio::http::ws::WebSocket::sendBinary(const std::span<const std::byte> data) {
    return writeMessage({Opcode::Binary, std::vector<std::byte>{data.begin(), data.end()}});
}

asyncio::task::Task<void, std::error_code> asyncio::http::ws::WebSocket::close(const CloseCode code) {
    assert(mState == State::Connected);

    mState = State::Closing;
    const auto c = htons(static_cast<std::uint16_t>(code));

    Z_CO_EXPECT(co_await writeInternalMessage({
        Opcode::Close,
        {reinterpret_cast<const std::byte *>(&c), reinterpret_cast<const std::byte *>(&c) + sizeof(c)}}
    ));

    while (true) {
        const auto message = co_await readInternalMessage();
        Z_CO_EXPECT(message);

        if (message->opcode == Opcode::Close) {
            mState = State::Closed;
            break;
        }
    }

    Z_CO_EXPECT(co_await mCloseable->close());
    co_return {};
}

Z_DEFINE_ERROR_CATEGORY_INSTANCES(asyncio::http::ws::WebSocket::Error, asyncio::http::ws::CloseCode)

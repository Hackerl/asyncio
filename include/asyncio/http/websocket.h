#ifndef ASYNCIO_WEBSOCKET_H
#define ASYNCIO_WEBSOCKET_H

#include <variant>
#include <asyncio/io.h>
#include <asyncio/http/url.h>
#include <asyncio/sync/mutex.h>
#include <zlib.h>

namespace asyncio::http::ws {
    enum class CloseCode {
        NORMAL_CLOSURE = 1000,
        GOING_AWAY,
        PROTOCOL_ERROR,
        UNSUPPORTED_DATA,
        NO_STATUS_RECEIVED = 1005,
        ABNORMAL_CLOSURE,
        INVALID_FRAME_PAYLOAD_DATA,
        POLICY_VIOLATION,
        MESSAGE_TOO_BIG,
        MANDATORY_EXTENSION,
        INTERNAL_ERROR,
        SERVICE_RESTART,
        TRY_AGAIN_LATER,
        BAD_GATEWAY
    };

    class CloseCodeCategory final : public std::error_category {
    public:
        static const std::error_category &instance();

        [[nodiscard]] const char *name() const noexcept override {
            return "asyncio::http::ws::WebSocket::close";
        }

        [[nodiscard]] std::string message(const int value) const override {
            switch (static_cast<CloseCode>(value)) {
            case CloseCode::NORMAL_CLOSURE:
                return "normal closure";

            case CloseCode::GOING_AWAY:
                return "going away";

            case CloseCode::PROTOCOL_ERROR:
                return "protocol error";

            case CloseCode::UNSUPPORTED_DATA:
                return "unsupported data";

            case CloseCode::NO_STATUS_RECEIVED:
                return "no status received";

            case CloseCode::ABNORMAL_CLOSURE:
                return "abnormal closure";

            case CloseCode::INVALID_FRAME_PAYLOAD_DATA:
                return "invalid frame payload data";

            case CloseCode::POLICY_VIOLATION:
                return "policy violation";

            case CloseCode::MESSAGE_TOO_BIG:
                return "message too big";

            case CloseCode::MANDATORY_EXTENSION:
                return "mandatory extension";

            case CloseCode::INTERNAL_ERROR:
                return "internal error";

            case CloseCode::SERVICE_RESTART:
                return "service restart";

            case CloseCode::TRY_AGAIN_LATER:
                return "try again later";

            case CloseCode::BAD_GATEWAY:
                return "bad gateway";

            default:
                return "unknown";
            }
        }
    };

    inline std::error_code make_error_code(const CloseCode e) {
        return {std::to_underlying(e), zero::error::categoryInstance<CloseCodeCategory>()};
    }

    enum class Opcode {
        CONTINUATION = 0,
        TEXT = 1,
        BINARY = 2,
        CLOSE = 8,
        PING = 9,
        PONG = 10
    };

    struct InternalMessage {
        Opcode opcode{};
        std::vector<std::byte> data;
    };

    struct Message {
        Opcode opcode{};
        std::variant<std::string, std::vector<std::byte>> data;
    };

    class Header {
    public:
        [[nodiscard]] Opcode opcode() const;
        [[nodiscard]] bool rsv1() const;
        [[nodiscard]] bool final() const;
        [[nodiscard]] std::size_t length() const;
        [[nodiscard]] bool mask() const;

        void opcode(Opcode opcode);
        void rsv1(bool rsv1);
        void final(bool final);
        void length(std::size_t length);
        void mask(bool mask);

    private:
        std::array<std::byte, 2> mBytes{};
    };

    struct Frame {
        Header header;
        std::vector<std::byte> data;
    };

    class Compressor {
    public:
        explicit Compressor(std::unique_ptr<z_stream, void (*)(z_stream *)> stream);

        static std::expected<Compressor, std::error_code> make(int windowBits);

        task::Task<std::vector<std::byte>, std::error_code> compress(std::span<const std::byte> data);
        std::expected<void, std::error_code> reset();

    private:
        std::unique_ptr<z_stream, void (*)(z_stream *)> mStream;
    };

    class Decompressor {
    public:
        explicit Decompressor(std::unique_ptr<z_stream, void (*)(z_stream *)> stream);

        static std::expected<Decompressor, std::error_code> make(int windowBits);

        task::Task<std::vector<std::byte>, std::error_code> decompress(std::span<const std::byte> data);
        std::expected<void, std::error_code> reset();

    private:
        std::unique_ptr<z_stream, void (*)(z_stream *)> mStream;
    };

    struct DeflateConfig {
        bool serverNoContextTakeover{false};
        bool clientNoContextTakeover{false};
        int serverMaxWindowBits{15};
        int clientMaxWindowBits{15};
    };

    struct DeflateExtension {
        DeflateConfig config;
        Compressor compressor;
        Decompressor decompressor;
    };

    class WebSocket {
        enum class State {
            CONNECTED,
            CLOSING,
            CLOSED
        };

    public:
        Z_DEFINE_ERROR_CODE_INNER(
            Error,
            "asyncio::http::ws::WebSocket",
            INVALID_URL, "invalid url",
            UNSUPPORTED_SCHEME, "unsupported websocket scheme",
            INVALID_RESPONSE, "invalid http response",
            UNEXPECTED_STATUS_CODE, "unexpected http response status code",
            INVALID_HTTP_HEADER, "invalid http header",
            NO_ACCEPT_HEADER, "no websocket accept header",
            HASH_MISMATCH, "hash mismatch",
            UNSUPPORTED_MASKED_FRAME, "unsupported masked frame",
            UNSUPPORTED_OPCODE, "unsupported opcode",
            CONNECTION_CLOSED, "connection closed",
            UNEXPECTED_COMPRESSED_MESSAGE, "unexpected compressed message"
        )

        WebSocket(
            std::shared_ptr<IReader> reader,
            std::shared_ptr<IWriter> writer,
            std::shared_ptr<ICloseable> closeable,
            std::optional<DeflateExtension> deflateExtension
        );

        static task::Task<WebSocket, std::error_code> connect(URL url);

    private:
        [[nodiscard]] task::Task<Frame, std::error_code> readFrame();
        [[nodiscard]] task::Task<InternalMessage, std::error_code> readInternalMessage();
        [[nodiscard]] task::Task<void, std::error_code> writeInternalMessage(InternalMessage message);

    public:
        [[nodiscard]] task::Task<Message, std::error_code> readMessage();
        [[nodiscard]] task::Task<void, std::error_code> writeMessage(Message message);

        [[nodiscard]] task::Task<void, std::error_code> sendText(std::string text);
        [[nodiscard]] task::Task<void, std::error_code> sendBinary(std::span<const std::byte> data);

        task::Task<void, std::error_code> close(CloseCode code);

    private:
        State mState;
        std::unique_ptr<sync::Mutex> mMutex;
        std::shared_ptr<IReader> mReader;
        std::shared_ptr<IWriter> mWriter;
        std::shared_ptr<ICloseable> mCloseable;
        std::optional<DeflateExtension> mDeflateExtension;
    };
}

Z_DECLARE_ERROR_CODES(asyncio::http::ws::CloseCode, asyncio::http::ws::WebSocket::Error)

#endif //ASYNCIO_WEBSOCKET_H

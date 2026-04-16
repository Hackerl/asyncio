#ifndef ASYNCIO_WEBSOCKET_H
#define ASYNCIO_WEBSOCKET_H

#include <variant>
#include <asyncio/io.h>
#include <asyncio/net/tls.h>
#include <asyncio/http/url.h>
#include <asyncio/sync/mutex.h>
#include <zlib.h>

namespace asyncio::http::ws {
    enum class CloseCode {
        NormalClosure = 1000,
        GoingAway,
        ProtocolError,
        UnsupportedData,
        NoStatusReceived = 1005,
        AbnormalClosure,
        InvalidFramePayloadData,
        PolicyViolation,
        MessageTooBig,
        MandatoryExtension,
        InternalError,
        ServiceRestart,
        TryAgainLater,
        BadGateway
    };

    class CloseCodeCategory final : public std::error_category {
    public:
        static const std::error_category &instance();

        [[nodiscard]] const char *name() const noexcept override {
            return "asyncio::http::ws::WebSocket::close";
        }

        [[nodiscard]] std::string message(const int value) const override {
            switch (static_cast<CloseCode>(value)) {
            case CloseCode::NormalClosure:
                return "Normal closure";

            case CloseCode::GoingAway:
                return "Going away";

            case CloseCode::ProtocolError:
                return "Protocol error";

            case CloseCode::UnsupportedData:
                return "Unsupported data";

            case CloseCode::NoStatusReceived:
                return "No status received";

            case CloseCode::AbnormalClosure:
                return "Abnormal closure";

            case CloseCode::InvalidFramePayloadData:
                return "Invalid frame payload data";

            case CloseCode::PolicyViolation:
                return "Policy violation";

            case CloseCode::MessageTooBig:
                return "Message too big";

            case CloseCode::MandatoryExtension:
                return "Mandatory extension";

            case CloseCode::InternalError:
                return "Internal error";

            case CloseCode::ServiceRestart:
                return "Service restart";

            case CloseCode::TryAgainLater:
                return "Try again later";

            case CloseCode::BadGateway:
                return "Bad gateway";

            default:
                return "Unknown";
            }
        }
    };

    inline std::error_code make_error_code(const CloseCode e) {
        return {std::to_underlying(e), zero::error::categoryInstance<CloseCodeCategory>()};
    }

    enum class Opcode {
        Continuation = 0,
        Text = 1,
        Binary = 2,
        Close = 8,
        Ping = 9,
        Pong = 10
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
        void reset();

    private:
        std::unique_ptr<z_stream, void (*)(z_stream *)> mStream;
    };

    class Decompressor {
    public:
        explicit Decompressor(std::unique_ptr<z_stream, void (*)(z_stream *)> stream);

        static std::expected<Decompressor, std::error_code> make(int windowBits);

        task::Task<std::vector<std::byte>, std::error_code> decompress(std::span<const std::byte> data);
        void reset();

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
            Connected,
            Closing,
            Closed
        };

    public:
        Z_DEFINE_ERROR_CODE_INNER(
            Error,
            "asyncio::http::ws::WebSocket",
            InvalidURL, "Invalid URL",
            UnsupportedScheme, "Unsupported WebSocket scheme",
            InvalidResponse, "Invalid HTTP response",
            UnexpectedStatusCode, "Unexpected HTTP response status code",
            InvalidHTTPHeader, "Invalid HTTP header",
            NoAcceptHeader, "No WebSocket accept header",
            HashMismatch, "Hash mismatch",
            UnsupportedMaskedFrame, "Unsupported masked frame",
            UnsupportedOpcode, "Unsupported opcode",
            ConnectionClosed, "Connection closed",
            UnexpectedCompressedMessage, "Unexpected compressed message"
        )

        WebSocket(
            std::shared_ptr<IReader> reader,
            std::shared_ptr<IWriter> writer,
            std::shared_ptr<ICloseable> closeable,
            std::optional<DeflateExtension> deflateExtension
        );

        static task::Task<WebSocket, std::error_code>
        connect(URL url, std::optional<net::tls::Context> context = std::nullopt);

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

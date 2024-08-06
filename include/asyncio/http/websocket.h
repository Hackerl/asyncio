#ifndef ASYNCIO_WEBSOCKET_H
#define ASYNCIO_WEBSOCKET_H

#include <variant>
#include <asyncio/io.h>
#include <asyncio/http/url.h>
#include <asyncio/sync/mutex.h>

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
        [[nodiscard]] const char *name() const noexcept override {
            return "asyncio::http::ws::WebSocket::close";
        }

        [[nodiscard]] std::string message(const int value) const override {
            std::string msg;

            switch (static_cast<CloseCode>(value)) {
            case CloseCode::NORMAL_CLOSURE:
                msg = "normal closure";
                break;

            case CloseCode::GOING_AWAY:
                msg = "going away";
                break;

            case CloseCode::PROTOCOL_ERROR:
                msg = "protocol error";
                break;

            case CloseCode::UNSUPPORTED_DATA:
                msg = "unsupported data";
                break;

            case CloseCode::NO_STATUS_RECEIVED:
                msg = "no status received";
                break;

            case CloseCode::ABNORMAL_CLOSURE:
                msg = "abnormal closure";
                break;

            case CloseCode::INVALID_FRAME_PAYLOAD_DATA:
                msg = "invalid frame payload data";
                break;

            case CloseCode::POLICY_VIOLATION:
                msg = "policy violation";
                break;

            case CloseCode::MESSAGE_TOO_BIG:
                msg = "message too big";
                break;

            case CloseCode::MANDATORY_EXTENSION:
                msg = "mandatory extension";
                break;

            case CloseCode::INTERNAL_ERROR:
                msg = "internal error";
                break;

            case CloseCode::SERVICE_RESTART:
                msg = "service restart";
                break;

            case CloseCode::TRY_AGAIN_LATER:
                msg = "try again later";
                break;

            case CloseCode::BAD_GATEWAY:
                msg = "bad gateway";
                break;

            default:
                msg = "unknown";
                break;
            }

            return msg;
        }
    };

    inline std::error_code make_error_code(const CloseCode e) {
        return {std::to_underlying(e), errorCategoryInstance<CloseCodeCategory>()};
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
        Opcode opcode;
        std::vector<std::byte> data;
    };

    struct Message {
        Opcode opcode{};
        std::variant<std::string, std::vector<std::byte>> data;
    };

    class Header {
    public:
        [[nodiscard]] Opcode opcode() const;
        [[nodiscard]] bool final() const;
        [[nodiscard]] std::size_t length() const;
        [[nodiscard]] bool mask() const;

        void opcode(Opcode opcode);
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

    class WebSocket {
        enum class State {
            CONNECTED,
            CLOSING,
            CLOSED
        };

    public:
        DEFINE_ERROR_CODE_INNER(
            Error,
            "asyncio::http::ws::WebSocket",
            UNSUPPORTED_SCHEME, "unsupported websocket scheme",
            INVALID_RESPONSE, "invalid http response",
            UNEXPECTED_STATUS_CODE, "unexpected http response status code",
            INVALID_HTTP_HEADER, "invalid http header",
            NO_ACCEPT_HEADER, "no websocket accept header",
            HASH_MISMATCH, "hash mismatch",
            UNSUPPORTED_MASKED_FRAME, "unsupported masked frame",
            UNSUPPORTED_OPCODE, "unsupported opcode",
            CONNECTION_CLOSED, "connection closed"
        )

        WebSocket(
            std::shared_ptr<IReader> reader,
            std::shared_ptr<IWriter> writer,
            std::shared_ptr<ICloseable> closeable
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
    };
}

DECLARE_ERROR_CODES(asyncio::http::ws::CloseCode, asyncio::http::ws::WebSocket::Error)

#endif //ASYNCIO_WEBSOCKET_H

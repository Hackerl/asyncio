#ifndef ASYNCIO_WEBSOCKET_H
#define ASYNCIO_WEBSOCKET_H

#include <variant>
#include <asyncio/http/url.h>
#include <asyncio/sync/mutex.h>

namespace asyncio::http::ws {
    enum class State {
        CONNECTED,
        CLOSING,
        CLOSED
    };

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
        std::byte mBytes[2]{};
    };

    struct Frame {
        Header header;
        std::vector<std::byte> data;
    };

    class WebSocket {
    public:
        DEFINE_ERROR_CODE_TYPES(
            Error,
            "asyncio::http::ws::WebSocket",
            UNSUPPORTED_MASKED_FRAME, "unsupported masked frame",
            UNSUPPORTED_OPCODE, "unsupported opcode",
            NOT_CONNECTED, "websocket not connected"
        )

        DEFINE_ERROR_CODE_TYPES(
            CloseCode,
            "asyncio::http::ws::WebSocket::close",
            NORMAL_CLOSURE, "normal closure",
            GOING_AWAY, "going away",
            PROTOCOL_ERROR, "protocol error",
            UNSUPPORTED_DATA, "unsupported data",
            NO_STATUS_RCVD, "no status rcvd",
            ABNORMAL_CLOSURE, "abnormal closure",
            INVALID_TEXT, "invalid text",
            POLICY_VIOLATION, "policy violation",
            MESSAGE_TOO_BIG, "message too big",
            MANDATORY_EXTENSION, "mandatory extension",
            INTERNAL_ERROR, "internal error",
            SERVICE_RESTART, "service restart",
            TRY_AGAIN_LATER, "try again later",
            BAD_GATEWAY, "bad gateway"
        )

        explicit WebSocket(std::unique_ptr<IBuffer> buffer);

    private:
        [[nodiscard]] task::Task<Frame, std::error_code> readFrame() const;
        [[nodiscard]] task::Task<InternalMessage, std::error_code> readInternalMessage() const;

        [[nodiscard]] task::Task<void, std::error_code>
        writeInternalMessage(InternalMessage message) const;

    public:
        [[nodiscard]] task::Task<Message, std::error_code> readMessage() const;
        [[nodiscard]] task::Task<void, std::error_code> writeMessage(Message message) const;

        [[nodiscard]] task::Task<void, std::error_code> sendText(std::string text) const;

        [[nodiscard]] task::Task<void, std::error_code>
        sendBinary(std::span<const std::byte> data) const;

        task::Task<void, std::error_code> close(CloseCode code);

    private:
        State mState;
        std::unique_ptr<sync::Mutex> mMutex;
        std::unique_ptr<IBuffer> mBuffer;
    };

    DEFINE_MAKE_ERROR_CODE(WebSocket::Error)
    DEFINE_MAKE_ERROR_CODE(WebSocket::CloseCode)

    DEFINE_ERROR_CODE(
        HandshakeError,
        "asyncio::http::ws::handshake",
        UNSUPPORTED_SCHEME, "unsupported websocket scheme",
        INVALID_RESPONSE, "invalid http response",
        UNEXPECTED_STATUS_CODE, "unexpected http response status code",
        INVALID_HTTP_HEADER, "invalid http header",
        NO_ACCEPT_HEADER, "no websocket accept header",
        HASH_MISMATCH, "hash mismatch"
    )

    task::Task<WebSocket, std::error_code> connect(URL url);
}

DECLARE_ERROR_CODES(
    asyncio::http::ws::WebSocket::Error,
    asyncio::http::ws::WebSocket::CloseCode,
    asyncio::http::ws::HandshakeError
)

#endif //ASYNCIO_WEBSOCKET_H

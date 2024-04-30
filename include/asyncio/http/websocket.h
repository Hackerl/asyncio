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
        enum class Error {
            UNSUPPORTED_MASKED_FRAME = 1,
            UNSUPPORTED_OPCODE,
            NOT_CONNECTED
        };

        class ErrorCategory final : public std::error_category {
        public:
            [[nodiscard]] const char *name() const noexcept override;
            [[nodiscard]] std::string message(int value) const override;
        };

        enum class CloseCode {
            NORMAL_CLOSURE = 1000,
            GOING_AWAY = 1001,
            PROTOCOL_ERROR = 1002,
            UNSUPPORTED_DATA = 1003,
            NO_STATUS_RCVD = 1005,
            ABNORMAL_CLOSURE = 1006,
            INVALID_TEXT = 1007,
            POLICY_VIOLATION = 1008,
            MESSAGE_TOO_BIG = 1009,
            MANDATORY_EXTENSION = 1010,
            INTERNAL_ERROR = 1011,
            SERVICE_RESTART = 1012,
            TRY_AGAIN_LATER = 1013,
            BAD_GATEWAY = 1014
        };

        class CloseCodeCategory final : public std::error_category {
        public:
            [[nodiscard]] const char *name() const noexcept override;
            [[nodiscard]] std::string message(int value) const override;
        };

        explicit WebSocket(std::unique_ptr<IBuffer> buffer);

    private:
        [[nodiscard]] zero::async::coroutine::Task<Frame, std::error_code> readFrame() const;
        [[nodiscard]] zero::async::coroutine::Task<InternalMessage, std::error_code> readInternalMessage() const;

        [[nodiscard]] zero::async::coroutine::Task<void, std::error_code>
        writeInternalMessage(InternalMessage message) const;

    public:
        [[nodiscard]] zero::async::coroutine::Task<Message, std::error_code> readMessage() const;
        [[nodiscard]] zero::async::coroutine::Task<void, std::error_code> writeMessage(Message message) const;

        [[nodiscard]] zero::async::coroutine::Task<void, std::error_code> sendText(std::string text) const;

        [[nodiscard]] zero::async::coroutine::Task<void, std::error_code>
        sendBinary(std::span<const std::byte> data) const;

        zero::async::coroutine::Task<void, std::error_code> close(CloseCode code);

    private:
        State mState;
        std::unique_ptr<sync::Mutex> mMutex;
        std::unique_ptr<IBuffer> mBuffer;
    };

    std::error_code make_error_code(WebSocket::Error e);
    std::error_code make_error_code(WebSocket::CloseCode e);

    enum class HandshakeError {
        UNSUPPORTED_SCHEME,
        INVALID_RESPONSE,
        UNEXPECTED_STATUS_CODE,
        INVALID_HTTP_HEADER,
        NO_ACCEPT_HEADER,
        HASH_MISMATCH
    };

    class HandshakeErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
    };

    std::error_code make_error_code(HandshakeError e);

    zero::async::coroutine::Task<WebSocket, std::error_code> connect(URL url);
}

template<>
struct std::is_error_code_enum<asyncio::http::ws::WebSocket::Error> : std::true_type {
};

template<>
struct std::is_error_code_enum<asyncio::http::ws::WebSocket::CloseCode> : std::true_type {
};

template<>
struct std::is_error_code_enum<asyncio::http::ws::HandshakeError> : std::true_type {
};

#endif //ASYNCIO_WEBSOCKET_H

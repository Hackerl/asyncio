#ifndef ASYNCIO_SSL_H
#define ASYNCIO_SSL_H

#include "stream.h"
#include <filesystem>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <event2/bufferevent_ssl.h>

namespace asyncio::net::ssl {
    DEFINE_ERROR_TRANSFORMER(
        Error,
        "asyncio::net::ssl",
        [](const int value) -> std::string {
            char buffer[1024];
            ERR_error_string_n(static_cast<unsigned long>(value), buffer, sizeof(buffer));

            return buffer;
        }
    )

    using Context = SSL_CTX;

#ifdef ASYNCIO_EMBED_CA_CERT
    std::expected<void, Error> loadEmbeddedCA(const Context *ctx);
#endif

    enum class Version {
        TLS_VERSION_1 = TLS1_VERSION,
        TLS_VERSION_1_1 = TLS1_1_VERSION,
        TLS_VERSION_1_2 = TLS1_2_VERSION,
        TLS_VERSION_1_3 = TLS1_3_VERSION,
        TLS_VERSION_3 = SSL3_VERSION
    };

    struct Config {
        std::optional<Version> minVersion;
        std::optional<Version> maxVersion;
        std::variant<std::monostate, std::string, std::filesystem::path> ca;
        std::variant<std::monostate, std::string, std::filesystem::path> cert;
        std::variant<std::monostate, std::string, std::filesystem::path> privateKey;
        bool insecure{};
        bool server{};
    };

    std::expected<std::shared_ptr<Context>, Error> newContext(const Config &config);

    namespace stream {
        enum class State {
            OPEN = BUFFEREVENT_SSL_OPEN,
            CONNECTING = BUFFEREVENT_SSL_CONNECTING,
            ACCEPTING = BUFFEREVENT_SSL_ACCEPTING
        };

        class Buffer final : public net::Buffer {
        public:
            Buffer(std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev, std::size_t capacity);

            static std::expected<Buffer, std::error_code>
            make(
                FileDescriptor fd,
                const std::shared_ptr<Context> &context,
                State state,
                std::size_t capacity = DEFAULT_BUFFER_CAPACITY,
                bool own = true
            );

        private:
            [[nodiscard]] std::error_code getError() const override;
        };

        class Listener : public net::Acceptor {
        public:
            Listener(std::shared_ptr<Context> context, evconnlistener *listener);

            task::Task<Buffer, std::error_code> accept();

        private:
            std::shared_ptr<Context> mContext;
        };

        std::expected<Listener, std::error_code> listen(const std::shared_ptr<Context> &context, const Address &address);

        std::expected<Listener, std::error_code>
        listen(const std::shared_ptr<Context> &context, std::span<const Address> addresses);

        std::expected<Listener, std::error_code>
        listen(const std::shared_ptr<Context> &context, const std::string &ip, unsigned short port);

        task::Task<Buffer, std::error_code> connect(Address address);
        task::Task<Buffer, std::error_code> connect(std::span<const Address> addresses);
        task::Task<Buffer, std::error_code> connect(std::string host, unsigned short port);

        task::Task<Buffer, std::error_code>
        connect(std::shared_ptr<Context> context, Address address);

        task::Task<Buffer, std::error_code>
        connect(std::shared_ptr<Context> context, std::span<const Address> addresses);

        task::Task<Buffer, std::error_code>
        connect(std::shared_ptr<Context> context, std::string host, unsigned short port);
    }
}

DECLARE_ERROR_CODE(asyncio::net::ssl::Error)

#endif //ASYNCIO_SSL_H

#ifndef ASYNCIO_SSL_H
#define ASYNCIO_SSL_H

#include "stream.h"
#include <filesystem>
#include <openssl/ssl.h>
#include <event2/bufferevent_ssl.h>

namespace asyncio::net::ssl {
    enum Error {

    };

    class Category : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
    };

    const std::error_category &category();
    std::error_code make_error_code(Error e);

    using Context = SSL_CTX;

#ifdef ASYNCIO_EMBED_CA_CERT
    tl::expected<void, std::error_code> loadEmbeddedCA(Context *ctx);
#endif

    enum Version {
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

    tl::expected<std::shared_ptr<Context>, std::error_code> newContext(const Config &config);

    namespace stream {
        enum State {
            OPEN = BUFFEREVENT_SSL_OPEN,
            CONNECTING = BUFFEREVENT_SSL_CONNECTING,
            ACCEPTING = BUFFEREVENT_SSL_ACCEPTING
        };

        class Buffer : public net::stream::Buffer {
        public:
            explicit Buffer(std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev);

        public:
            tl::expected<void, std::error_code> close() override;

        private:
            std::error_code getError() override;
        };

        tl::expected<Buffer, std::error_code>
        makeBuffer(evutil_socket_t fd, const std::shared_ptr<Context> &context, State state, bool own = true);

        class Listener : public net::stream::Acceptor {
        public:
            Listener(std::shared_ptr<Context> context, evconnlistener *listener);

        public:
            zero::async::coroutine::Task<Buffer, std::error_code> accept();

        private:
            std::shared_ptr<Context> mContext;
        };

        tl::expected<Listener, std::error_code> listen(const std::shared_ptr<Context> &context, const Address &address);

        tl::expected<Listener, std::error_code>
        listen(const std::shared_ptr<Context> &context, std::span<const Address> addresses);

        tl::expected<Listener, std::error_code>
        listen(const std::shared_ptr<Context> &context, const std::string &ip, unsigned short port);

        zero::async::coroutine::Task<Buffer, std::error_code>
        connect(Address address);

        zero::async::coroutine::Task<Buffer, std::error_code>
        connect(std::span<const Address> addresses);

        zero::async::coroutine::Task<Buffer, std::error_code>
        connect(std::string host, unsigned short port);

        zero::async::coroutine::Task<Buffer, std::error_code>
        connect(std::shared_ptr<Context> context, Address address);

        zero::async::coroutine::Task<Buffer, std::error_code>
        connect(std::shared_ptr<Context> context, std::span<const Address> addresses);

        zero::async::coroutine::Task<Buffer, std::error_code>
        connect(std::shared_ptr<Context> context, std::string host, unsigned short port);
    }
}

namespace std {
    template<>
    struct is_error_code_enum<asyncio::net::ssl::Error> : public true_type {

    };
}

#endif //ASYNCIO_SSL_H

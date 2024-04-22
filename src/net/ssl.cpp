#include <asyncio/net/ssl.h>
#include <asyncio/net/dns.h>
#include <asyncio/promise.h>
#include <zero/defer.h>
#include <zero/os/net.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

#ifdef ASYNCIO_EMBED_CA_CERT
#include <cacert.h>
#endif

static const auto defaultContext = asyncio::net::ssl::newContext({});

const char *asyncio::net::ssl::ErrorCategory::name() const noexcept {
    return "asyncio::net::ssl";
}

std::string asyncio::net::ssl::ErrorCategory::message(const int value) const {
    char buffer[1024];
    ERR_error_string_n(static_cast<unsigned long>(value), buffer, sizeof(buffer));

    return buffer;
}

const std::error_category &asyncio::net::ssl::errorCategory() {
    static ErrorCategory instance;
    return instance;
}

std::error_code asyncio::net::ssl::make_error_code(const Error e) {
    return {static_cast<int>(e), errorCategory()};
}

#ifdef ASYNCIO_EMBED_CA_CERT
tl::expected<void, std::error_code> asyncio::net::ssl::loadEmbeddedCA(const Context *ctx) {
    BIO *bio = BIO_new_mem_buf(CA_CERT, sizeof(CA_CERT));

    if (!bio)
        return tl::unexpected(static_cast<Error>(ERR_get_error()));

    DEFER(BIO_free(bio));
    STACK_OF(X509_INFO) *info = PEM_X509_INFO_read_bio(bio, nullptr, nullptr, nullptr);

    if (!info)
        return tl::unexpected(static_cast<Error>(ERR_get_error()));

    DEFER(sk_X509_INFO_pop_free(info, X509_INFO_free));
    X509_STORE *store = SSL_CTX_get_cert_store(ctx);

    if (!store)
        return tl::unexpected(static_cast<Error>(ERR_get_error()));

    for (int i = 0; i < sk_X509_INFO_num(info); i++) {
        const X509_INFO *item = sk_X509_INFO_value(info, i);

        if (item->x509)
            X509_STORE_add_cert(store, item->x509);

        if (item->crl)
            X509_STORE_add_crl(store, item->crl);
    }

    return {};
}
#endif

std::shared_ptr<EVP_PKEY> readPrivateKey(const std::string_view content) {
    BIO *bio = BIO_new_mem_buf(content.data(), static_cast<int>(content.length()));

    if (!bio)
        return nullptr;

    DEFER(BIO_free(bio));
    EVP_PKEY *key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);

    if (!key)
        return nullptr;

    return {key, EVP_PKEY_free};
}

std::shared_ptr<X509> readCertificate(const std::string_view content) {
    BIO *bio = BIO_new_mem_buf(content.data(), static_cast<int>(content.length()));

    if (!bio)
        return nullptr;

    DEFER(BIO_free(bio));
    X509 *cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

    if (!cert)
        return nullptr;

    return {cert, X509_free};
}

tl::expected<std::shared_ptr<asyncio::net::ssl::Context>, std::error_code>
asyncio::net::ssl::newContext(const Config &config) {
    std::shared_ptr<Context> ctx(
        SSL_CTX_new(TLS_method()),
        [](Context *context) {
            SSL_CTX_free(context);
        }
    );

    if (!ctx)
        return tl::unexpected(static_cast<Error>(ERR_get_error()));

    if (!SSL_CTX_set_min_proto_version(ctx.get(), config.minVersion.value_or(TLS_VERSION_1_2)))
        return tl::unexpected(static_cast<Error>(ERR_get_error()));

    if (!SSL_CTX_set_max_proto_version(ctx.get(), config.minVersion.value_or(TLS_VERSION_1_3)))
        return tl::unexpected(static_cast<Error>(ERR_get_error()));

    if (std::holds_alternative<std::string>(config.ca)) {
        const auto cert = readCertificate(std::get<std::string>(config.ca));

        if (!cert)
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        X509_STORE *store = SSL_CTX_get_cert_store(ctx.get());

        if (!store)
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        if (!X509_STORE_add_cert(store, cert.get()))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));
    }
    else if (std::holds_alternative<std::filesystem::path>(config.ca)) {
        if (!SSL_CTX_load_verify_locations(
            ctx.get(),
            std::get<std::filesystem::path>(config.ca).string().c_str(),
            nullptr
        ))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));
    }

    if (std::holds_alternative<std::string>(config.cert)) {
        const auto cert = readCertificate(std::get<std::string>(config.cert));

        if (!cert)
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        if (!SSL_CTX_use_certificate(ctx.get(), cert.get()))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));
    }
    else if (std::holds_alternative<std::filesystem::path>(config.cert)) {
        if (!SSL_CTX_use_certificate_file(
            ctx.get(),
            std::get<std::filesystem::path>(config.cert).string().c_str(),
            SSL_FILETYPE_PEM
        ))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));
    }

    if (std::holds_alternative<std::string>(config.privateKey)) {
        const auto key = readPrivateKey(std::get<std::string>(config.privateKey));

        if (!key)
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        if (!SSL_CTX_use_PrivateKey(ctx.get(), key.get()))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        if (!SSL_CTX_check_private_key(ctx.get()))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));
    }
    else if (std::holds_alternative<std::filesystem::path>(config.privateKey)) {
        if (!SSL_CTX_use_PrivateKey_file(
            ctx.get(),
            std::get<std::filesystem::path>(config.privateKey).string().c_str(),
            SSL_FILETYPE_PEM
        ))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        if (!SSL_CTX_check_private_key(ctx.get()))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));
    }

    if (!config.insecure && std::holds_alternative<std::monostate>(config.ca) && !config.server) {
#ifdef ASYNCIO_EMBED_CA_CERT
        EXPECT(loadEmbeddedCA(ctx.get()));
#else
        if (!SSL_CTX_set_default_verify_paths(ctx.get()))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));
#endif
    }

    SSL_CTX_set_verify(
        ctx.get(),
        config.insecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER | (config.server ? SSL_VERIFY_FAIL_IF_NO_PEER_CERT : 0),
        nullptr
    );

    return ctx;
}

asyncio::net::ssl::stream::Buffer::Buffer(
    std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev,
    const std::size_t capacity
) : net::stream::Buffer(std::move(bev), capacity) {
}

tl::expected<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::Buffer::make(
    const FileDescriptor fd,
    const std::shared_ptr<Context> &context,
    State state,
    const std::size_t capacity,
    const bool own
) {
    bufferevent *bev = bufferevent_openssl_socket_new(
        getEventLoop()->base(),
        fd,
        SSL_new(context.get()),
        static_cast<bufferevent_ssl_state>(state),
        own ? BEV_OPT_CLOSE_ON_FREE : 0
    );

    if (!bev)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    return Buffer{
        {
            bev,
            [](bufferevent *b) {
                SSL *ctx = bufferevent_openssl_get_ssl(b);
                SSL_set_shutdown(ctx, SSL_RECEIVED_SHUTDOWN);
                SSL_shutdown(ctx);
                bufferevent_free(b);
            }
        },
        capacity
    };
}

std::error_code asyncio::net::ssl::stream::Buffer::getError() const {
    const unsigned long e = bufferevent_get_openssl_error(mBev.get());

    if (!e)
        return {EVUTIL_SOCKET_ERROR(), std::system_category()};

    return static_cast<Error>(e);
}

asyncio::net::ssl::stream::Listener::Listener(std::shared_ptr<Context> context, evconnlistener *listener)
    : Acceptor(listener), mContext(std::move(context)) {
}

zero::async::coroutine::Task<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::Listener::accept() {
    const auto result = co_await fd();
    CO_EXPECT(result);
    co_return Buffer::make(*result, mContext, ACCEPTING);
}

tl::expected<asyncio::net::ssl::stream::Listener, std::error_code>
asyncio::net::ssl::stream::listen(const std::shared_ptr<Context> &context, const Address &address) {
    const auto socketAddress = socketAddressFrom(address);
    EXPECT(socketAddress);

    evconnlistener *listener = evconnlistener_new_bind(
        getEventLoop()->base(),
        nullptr,
        nullptr,
        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE | LEV_OPT_DISABLED,
        -1,
        socketAddress->first.get(),
#ifdef _WIN32
        socketAddress->second
#else
        static_cast<int>(socketAddress->second)
#endif
    );

    if (!listener)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    return Listener{context, listener};
}

tl::expected<asyncio::net::ssl::stream::Listener, std::error_code>
asyncio::net::ssl::stream::listen(const std::shared_ptr<Context> &context, std::span<const Address> addresses) {
    if (addresses.empty())
        return tl::unexpected(make_error_code(std::errc::invalid_argument));

    auto it = addresses.begin();

    while (true) {
        auto result = listen(context, *it);

        if (result)
            return result;

        if (++it == addresses.end())
            return tl::unexpected(result.error());
    }
}

tl::expected<asyncio::net::ssl::stream::Listener, std::error_code>
asyncio::net::ssl::stream::listen(
    const std::shared_ptr<Context> &context,
    const std::string &ip,
    const unsigned short port
) {
    const auto address = addressFrom(ip, port);
    EXPECT(address);
    return listen(context, *address);
}

zero::async::coroutine::Task<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::connect(Address address) {
    if (!defaultContext)
        co_return tl::unexpected(defaultContext.error());

    co_return co_await connect(*defaultContext, std::move(address));
}

zero::async::coroutine::Task<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::connect(const std::span<const Address> addresses) {
    if (!defaultContext)
        co_return tl::unexpected(defaultContext.error());

    co_return co_await connect(*defaultContext, addresses);
}

zero::async::coroutine::Task<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::connect(std::string host, const unsigned short port) {
    if (!defaultContext)
        co_return tl::unexpected(defaultContext.error());

    co_return co_await connect(*defaultContext, std::move(host), port);
}

zero::async::coroutine::Task<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::connect(std::shared_ptr<Context> context, const Address address) {
    if (std::holds_alternative<IPv4Address>(address)) {
        const auto [port, ip] = std::get<IPv4Address>(address);
        co_return co_await connect(std::move(context), zero::os::net::stringify(ip), port);
    }

    if (std::holds_alternative<IPv6Address>(address)) {
        const auto &[port, ip, zone] = std::get<IPv6Address>(address);
        co_return co_await connect(std::move(context), zero::os::net::stringify(ip), port);
    }

    co_return tl::unexpected(make_error_code(std::errc::address_family_not_supported));
}

zero::async::coroutine::Task<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::connect(const std::shared_ptr<Context> context, std::span<const Address> addresses) {
    if (addresses.empty())
        co_return tl::unexpected(make_error_code(std::errc::invalid_argument));

    auto it = addresses.begin();

    while (true) {
        auto result = co_await connect(context, *it);

        if (result)
            co_return *std::move(result);

        if (++it == addresses.end())
            co_return tl::unexpected(result.error());
    }
}

zero::async::coroutine::Task<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::connect(
    const std::shared_ptr<Context> context,
    const std::string host,
    const unsigned short port
) {
    const auto dnsBase = getEventLoop()->makeDNSBase();
    CO_EXPECT(dnsBase);

    SSL *ssl = SSL_new(context.get());

    if (!ssl)
        co_return tl::unexpected(static_cast<Error>(ERR_get_error()));

    DEFER(
        if (ssl)
            SSL_free(ssl);
    );

    SSL_set_tlsext_host_name(ssl, host.c_str());
    SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);

    if (!SSL_set1_host(ssl, host.c_str()))
        co_return tl::unexpected(static_cast<Error>(ERR_get_error()));

    bufferevent *bev = bufferevent_openssl_socket_new(
        getEventLoop()->base(),
        -1,
        std::exchange(ssl, nullptr),
        BUFFEREVENT_SSL_CONNECTING,
        BEV_OPT_CLOSE_ON_FREE
    );

    if (!bev)
        co_return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    DEFER(
        if (bev)
            bufferevent_free(bev);
    );

    Promise<void, std::error_code> promise;

    bufferevent_setcb(
        bev,
        nullptr,
        nullptr,
        [](bufferevent *b, const short what, void *arg) {
            const auto p = static_cast<Promise<void, std::error_code> *>(arg);
            assert(!p->isFulfilled());

            if ((what & BEV_EVENT_CONNECTED) == 0) {
                if (const int e = bufferevent_socket_get_dns_error(b)) {
                    p->reject(static_cast<dns::Error>(e));
                    return;
                }

                if (const unsigned long e = bufferevent_get_openssl_error(b)) {
                    p->reject(static_cast<Error>(e));
                    return;
                }

                p->reject(EVUTIL_SOCKET_ERROR(), std::system_category());
                return;
            }

            p->resolve();
        },
        &promise
    );

    if (bufferevent_socket_connect_hostname(bev, dnsBase->get(), AF_UNSPEC, host.c_str(), port) < 0)
        co_return tl::unexpected(make_error_code(std::errc::invalid_argument));

    CO_EXPECT(co_await zero::async::coroutine::Cancellable{
        promise.getFuture(),
        [&]() -> tl::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return tl::unexpected(make_error_code(std::errc::operation_not_supported));

            bufferevent_free(std::exchange(bev, nullptr));
            promise.reject(make_error_code(std::errc::operation_canceled));
            return {};
        }
    });

    co_return Buffer{
        {
            std::exchange(bev, nullptr),
            [](bufferevent *b) {
                SSL *s = bufferevent_openssl_get_ssl(b);
                SSL_set_shutdown(s, SSL_RECEIVED_SHUTDOWN);
                SSL_shutdown(s);
                bufferevent_free(b);
            }
        },
        DEFAULT_BUFFER_CAPACITY
    };
}

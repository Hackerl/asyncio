#include <asyncio/net/ssl.h>
#include <asyncio/net/dns.h>
#include <asyncio/event_loop.h>
#include <asyncio/error.h>
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
    auto ctx = std::shared_ptr<Context>(
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

    switch (config.ca.index()) {
    case 1: {
        const auto cert = readCertificate(std::get<1>(config.ca));

        if (!cert)
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        X509_STORE *store = SSL_CTX_get_cert_store(ctx.get());

        if (!store)
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        if (!X509_STORE_add_cert(store, cert.get()))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        break;
    }

    case 2:
        if (!SSL_CTX_load_verify_locations(ctx.get(), std::get<2>(config.ca).string().c_str(), nullptr))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        break;

    default:
        break;
    }

    switch (config.cert.index()) {
    case 1: {
        const auto cert = readCertificate(std::get<1>(config.cert));

        if (!cert)
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        if (!SSL_CTX_use_certificate(ctx.get(), cert.get()))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        break;
    }

    case 2:
        if (!SSL_CTX_use_certificate_file(ctx.get(), std::get<2>(config.cert).string().c_str(), SSL_FILETYPE_PEM))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        break;

    default:
        break;
    }

    switch (config.privateKey.index()) {
    case 1: {
        const auto key = readPrivateKey(std::get<1>(config.privateKey));

        if (!key)
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        if (!SSL_CTX_use_PrivateKey(ctx.get(), key.get()))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        if (!SSL_CTX_check_private_key(ctx.get()))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        break;
    }

    case 2:
        if (!SSL_CTX_use_PrivateKey_file(
            ctx.get(),
            std::get<2>(config.privateKey).string().c_str(),
            SSL_FILETYPE_PEM
        ))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        if (!SSL_CTX_check_private_key(ctx.get()))
            return tl::unexpected(static_cast<Error>(ERR_get_error()));

        break;

    default:
        break;
    }

    if (!config.insecure && config.ca.index() == 0 && !config.server) {
#ifdef ASYNCIO_EMBED_CA_CERT
        TRY(loadEmbeddedCA(ctx.get()));
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
): net::stream::Buffer(std::move(bev), capacity) {
}

zero::async::coroutine::Task<void, std::error_code> asyncio::net::ssl::stream::Buffer::close() {
    if (!mBev)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mClosed)
        co_return tl::unexpected(IO_EOF);

    SSL *ctx = bufferevent_openssl_get_ssl(mBev.get());
    SSL_set_shutdown(ctx, SSL_RECEIVED_SHUTDOWN);
    SSL_shutdown(ctx);

    co_return co_await net::stream::Buffer::close();
}

std::error_code asyncio::net::ssl::stream::Buffer::getError() const {
    unsigned long e = bufferevent_get_openssl_error(mBev.get());

    if (!e)
        return {EVUTIL_SOCKET_ERROR(), std::system_category()};

    return static_cast<Error>(e);
}

tl::expected<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::makeBuffer(
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
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

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

asyncio::net::ssl::stream::Listener::Listener(std::shared_ptr<Context> context, evconnlistener *listener)
    : Acceptor(listener), mContext(std::move(context)) {
}

zero::async::coroutine::Task<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::Listener::accept() {
    auto result = CO_TRY(co_await fd());
    co_return makeBuffer(*result, mContext, ACCEPTING);
}

tl::expected<asyncio::net::ssl::stream::Listener, std::error_code>
asyncio::net::ssl::stream::listen(const std::shared_ptr<Context> &context, const Address &address) {
    const auto socketAddress = TRY(socketAddressFrom(address));

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
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

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
    auto address = TRY(addressFrom(ip, port));
    return listen(context, *address);
}

zero::async::coroutine::Task<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::connect(Address address) {
    if (!defaultContext)
        co_return tl::unexpected(defaultContext.error());

    co_return std::move(co_await connect(*defaultContext, std::move(address)));
}

zero::async::coroutine::Task<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::connect(const std::span<const Address> addresses) {
    if (!defaultContext)
        co_return tl::unexpected(defaultContext.error());

    co_return std::move(co_await connect(*defaultContext, addresses));
}

zero::async::coroutine::Task<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::connect(std::string host, const unsigned short port) {
    if (!defaultContext)
        co_return tl::unexpected(defaultContext.error());

    co_return std::move(co_await connect(*defaultContext, std::move(host), port));
}

zero::async::coroutine::Task<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::connect(std::shared_ptr<Context> context, const Address address) {
    if (const std::size_t index = address.index(); index == 0) {
        const auto [port, ip] = std::get<IPv4Address>(address);
        co_return std::move(co_await connect(std::move(context), zero::os::net::stringify(ip), port));
    }
    else if (index == 1) {
        const auto &[port, ip, zone] = std::get<IPv6Address>(address);
        co_return std::move(co_await connect(std::move(context), zero::os::net::stringify(ip), port));
    }

    co_return tl::unexpected(make_error_code(std::errc::address_family_not_supported));
}

zero::async::coroutine::Task<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::connect(const std::shared_ptr<Context> context, std::span<const Address> addresses) {
    if (addresses.empty())
        co_return tl::unexpected(make_error_code(std::errc::invalid_argument));

    auto it = addresses.begin();

    while (true) {
        auto result = std::move(co_await connect(context, *it));

        if (result)
            co_return std::move(*result);

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
    SSL *ssl = SSL_new(context.get());

    if (!ssl)
        co_return tl::unexpected(static_cast<Error>(ERR_get_error()));

    SSL_set_tlsext_host_name(ssl, host.c_str());
    SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);

    if (!SSL_set1_host(ssl, host.c_str())) {
        SSL_free(ssl);
        co_return tl::unexpected(static_cast<Error>(ERR_get_error()));
    }

    bufferevent *bev = bufferevent_openssl_socket_new(
        getEventLoop()->base(),
        -1,
        ssl,
        BUFFEREVENT_SSL_CONNECTING,
        BEV_OPT_CLOSE_ON_FREE
    );

    if (!bev)
        co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    zero::async::promise::Promise<void, std::error_code> promise;
    const auto ctx = new zero::async::promise::Promise(promise);

    bufferevent_setcb(
        bev,
        nullptr,
        nullptr,
        [](bufferevent *b, const short what, void *arg) {
            const auto p = static_cast<zero::async::promise::Promise<void, std::error_code> *>(arg);

            if ((what & BEV_EVENT_CONNECTED) == 0) {
                if (const int e = bufferevent_socket_get_dns_error(b)) {
                    p->reject(static_cast<dns::Error>(e));
                    delete p;
                    return;
                }

                if (const unsigned long e = bufferevent_get_openssl_error(b)) {
                    p->reject(static_cast<Error>(e));
                    delete p;
                    return;
                }

                p->reject(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
                delete p;
                return;
            }

            p->resolve();
            delete p;
        },
        ctx
    );

    if (bufferevent_socket_connect_hostname(bev, getEventLoop()->dnsBase(), AF_UNSPEC, host.c_str(), port) < 0) {
        delete ctx;
        bufferevent_free(bev);
        co_return tl::unexpected(make_error_code(std::errc::invalid_argument));
    }

    if (const auto result = co_await zero::async::coroutine::Cancellable{
        promise,
        [=]() mutable -> tl::expected<void, std::error_code> {
            bufferevent_setcb(bev, nullptr, nullptr, nullptr, nullptr);
            promise.reject(make_error_code(std::errc::operation_canceled));
            delete ctx;
            return {};
        }
    }; !result) {
        bufferevent_free(bev);
        co_return tl::unexpected(result.error());
    }

    co_return Buffer{
        {
            bev,
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

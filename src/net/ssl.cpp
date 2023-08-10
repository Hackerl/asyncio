#include <asyncio/net/ssl.h>
#include <asyncio/error.h>
#include <asyncio/event_loop.h>
#include <zero/os/net.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

#ifdef ASYNCIO_EMBED_CA_CERT
#include <cacert.h>
#endif

static auto defaultContext = asyncio::net::ssl::newContext({});

const char *asyncio::net::ssl::Category::name() const noexcept {
    return "asyncio::net::ssl";
}

std::string asyncio::net::ssl::Category::message(int value) const {
    char buffer[1024];
    ERR_error_string_n((unsigned long) value, buffer, sizeof(buffer));

    return buffer;
}

const std::error_category &asyncio::net::ssl::category() {
    static Category instance;
    return instance;
}

std::error_code asyncio::net::ssl::make_error_code(asyncio::net::ssl::Error e) {
    return {static_cast<int>(e), category()};
}

#ifdef ASYNCIO_EMBED_CA_CERT
tl::expected<void, std::error_code> asyncio::net::ssl::loadEmbeddedCA(Context *ctx) {
    BIO *bio = BIO_new_mem_buf(CA_CERT, (int) sizeof(CA_CERT));

    if (!bio)
        return tl::unexpected(make_error_code((Error) ERR_get_error()));

    STACK_OF(X509_INFO) *info = PEM_X509_INFO_read_bio(bio, nullptr, nullptr, nullptr);

    if (!info) {
        BIO_free(bio);
        return tl::unexpected(make_error_code((Error) ERR_get_error()));
    }

    X509_STORE *store = SSL_CTX_get_cert_store(ctx);

    if (!store) {
        sk_X509_INFO_pop_free(info, X509_INFO_free);
        BIO_free(bio);
        return tl::unexpected(make_error_code((Error) ERR_get_error()));
    }

    for (int i = 0; i < sk_X509_INFO_num(info); i++) {
        X509_INFO *item = sk_X509_INFO_value(info, i);

        if (item->x509)
            X509_STORE_add_cert(store, item->x509);

        if (item->crl)
            X509_STORE_add_crl(store, item->crl);
    }

    sk_X509_INFO_pop_free(info, X509_INFO_free);
    BIO_free(bio);

    return {};
}
#endif

std::shared_ptr<EVP_PKEY> readPrivateKey(std::string_view content) {
    BIO *bio = BIO_new_mem_buf(content.data(), (int) content.length());

    if (!bio)
        return nullptr;

    EVP_PKEY *key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);

    if (!key) {
        BIO_free(bio);
        return nullptr;
    }

    BIO_free(bio);
    return {key, EVP_PKEY_free};
}

std::shared_ptr<X509> readCertificate(std::string_view content) {
    BIO *bio = BIO_new_mem_buf(content.data(), (int) content.length());

    if (!bio)
        return nullptr;

    X509 *cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

    if (!cert) {
        BIO_free(bio);
        return nullptr;
    }

    BIO_free(bio);
    return {cert, X509_free};
}

tl::expected<std::shared_ptr<asyncio::net::ssl::Context>, std::error_code>
asyncio::net::ssl::newContext(const Config &config) {
    std::shared_ptr<Context> ctx = std::shared_ptr<Context>(
            SSL_CTX_new(TLS_method()),
            [](Context *context) {
                SSL_CTX_free(context);
            }
    );

    if (!ctx)
        return tl::unexpected(make_error_code((Error) ERR_get_error()));

    if (!SSL_CTX_set_min_proto_version(ctx.get(), config.minVersion.value_or(TLS_VERSION_1_2)))
        return tl::unexpected(make_error_code((Error) ERR_get_error()));

    if (!SSL_CTX_set_max_proto_version(ctx.get(), config.minVersion.value_or(TLS_VERSION_1_3)))
        return tl::unexpected(make_error_code((Error) ERR_get_error()));

    switch (config.ca.index()) {
        case 1: {
            std::shared_ptr<X509> cert = readCertificate(std::get<1>(config.ca));

            if (!cert)
                return tl::unexpected(make_error_code((Error) ERR_get_error()));

            X509_STORE *store = SSL_CTX_get_cert_store(ctx.get());

            if (!store)
                return tl::unexpected(make_error_code((Error) ERR_get_error()));

            if (!X509_STORE_add_cert(store, cert.get()))
                return tl::unexpected(make_error_code((Error) ERR_get_error()));

            break;
        }

        case 2:
            if (!SSL_CTX_load_verify_locations(ctx.get(), std::get<2>(config.ca).string().c_str(), nullptr))
                return tl::unexpected(make_error_code((Error) ERR_get_error()));

            break;

        default:
            break;
    }

    switch (config.cert.index()) {
        case 1: {
            std::shared_ptr<X509> cert = readCertificate(std::get<1>(config.cert));

            if (!cert)
                return tl::unexpected(make_error_code((Error) ERR_get_error()));

            if (!SSL_CTX_use_certificate(ctx.get(), cert.get()))
                return tl::unexpected(make_error_code((Error) ERR_get_error()));

            break;
        }

        case 2:
            if (!SSL_CTX_use_certificate_file(ctx.get(), std::get<2>(config.cert).string().c_str(), SSL_FILETYPE_PEM))
                return tl::unexpected(make_error_code((Error) ERR_get_error()));

            break;

        default:
            break;
    }

    switch (config.privateKey.index()) {
        case 1: {
            std::shared_ptr<EVP_PKEY> key = readPrivateKey(std::get<1>(config.privateKey));

            if (!key)
                return tl::unexpected(make_error_code((Error) ERR_get_error()));

            if (!SSL_CTX_use_PrivateKey(ctx.get(), key.get()))
                return tl::unexpected(make_error_code((Error) ERR_get_error()));

            if (!SSL_CTX_check_private_key(ctx.get()))
                return tl::unexpected(make_error_code((Error) ERR_get_error()));

            break;
        }

        case 2:
            if (!SSL_CTX_use_PrivateKey_file(
                    ctx.get(),
                    std::get<2>(config.privateKey).string().c_str(),
                    SSL_FILETYPE_PEM
            ))
                return tl::unexpected(make_error_code((Error) ERR_get_error()));

            if (!SSL_CTX_check_private_key(ctx.get()))
                return tl::unexpected(make_error_code((Error) ERR_get_error()));

            break;

        default:
            break;
    }

    if (!config.insecure && config.ca.index() == 0 && !config.server) {
#ifdef ASYNCIO_EMBED_CA_CERT
        auto result = loadEmbeddedCA(ctx.get());

        if (!result)
            return tl::unexpected(result.error());
#else
        if (!SSL_CTX_set_default_verify_paths(ctx.get()))
            return tl::unexpected(make_error_code((Error) ERR_get_error()));
#endif
    }

    SSL_CTX_set_verify(
            ctx.get(),
            config.insecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER | (config.server ? SSL_VERIFY_FAIL_IF_NO_PEER_CERT : 0),
            nullptr
    );

    return ctx;
}

asyncio::net::ssl::stream::Buffer::Buffer(bufferevent *bev) : net::stream::Buffer(bev) {

}

tl::expected<void, std::error_code> asyncio::net::ssl::stream::Buffer::close() {
    if (!mBev)
        return tl::unexpected(asyncio::Error::RESOURCE_DESTROYED);

    if (mClosed)
        return tl::unexpected(asyncio::Error::IO_EOF);

    SSL *ctx = bufferevent_openssl_get_ssl(mBev.get());
    SSL_set_shutdown(ctx, SSL_RECEIVED_SHUTDOWN);
    SSL_shutdown(ctx);

    return net::stream::Buffer::close();
}

tl::expected<asyncio::net::ssl::stream::Buffer, std::error_code>
asyncio::net::ssl::stream::makeBuffer(evutil_socket_t fd, const std::shared_ptr<Context> &context, State state, bool own) {
    bufferevent *bev = bufferevent_openssl_socket_new(
            getEventLoop()->base(),
            fd,
            SSL_new(context.get()),
            (bufferevent_ssl_state) state,
            own ? BEV_OPT_CLOSE_ON_FREE : 0
    );

    if (!bev)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return Buffer{bev};
}

asyncio::net::ssl::stream::Listener::Listener(std::shared_ptr<Context> context, evconnlistener *listener)
        : mContext(std::move(context)), Acceptor(listener) {

}

zero::async::coroutine::Task<std::shared_ptr<asyncio::net::stream::IBuffer>, std::error_code>
asyncio::net::ssl::stream::Listener::accept() {
    auto result = co_await fd();

    if (!result)
        co_return tl::unexpected(result.error());

    co_return makeBuffer(*result, mContext, State::ACCEPTING, true).transform([](Buffer &&buffer) {
        return std::make_shared<Buffer>(std::move(buffer));
    });
}

tl::expected<asyncio::net::ssl::stream::Listener, std::error_code>
asyncio::net::ssl::stream::listen(const std::shared_ptr<Context> &context, const Address &address) {
    auto socketAddress = socketAddressFrom(address);

    if (!socketAddress)
        return tl::unexpected(socketAddress.error());

    evconnlistener *listener = evconnlistener_new_bind(
            getEventLoop()->base(),
            nullptr,
            nullptr,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE | LEV_OPT_DISABLED,
            -1,
            (const sockaddr *) &*socketAddress,
            (int) sizeof(sockaddr_storage)
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

        if (it == addresses.end())
            return tl::unexpected(result.error());

        it++;
    }
}

tl::expected<asyncio::net::ssl::stream::Listener, std::error_code>
asyncio::net::ssl::stream::listen(const std::shared_ptr<Context> &context, const std::string &ip, unsigned short port) {
    auto address = addressFrom(ip, port);

    if (!address)
        return tl::unexpected(address.error());

    return listen(context, *address);
}

zero::async::coroutine::Task<std::shared_ptr<asyncio::net::stream::IBuffer>, std::error_code>
asyncio::net::ssl::stream::connect(const asyncio::net::Address &address) {
    if (!defaultContext)
        co_return tl::unexpected(defaultContext.error());

    co_return co_await connect(*defaultContext, address);
}

zero::async::coroutine::Task<std::shared_ptr<asyncio::net::stream::IBuffer>, std::error_code>
asyncio::net::ssl::stream::connect(std::span<const Address> addresses) {
    if (!defaultContext)
        co_return tl::unexpected(defaultContext.error());

    co_return co_await connect(*defaultContext, addresses);
}

zero::async::coroutine::Task<std::shared_ptr<asyncio::net::stream::IBuffer>, std::error_code>
asyncio::net::ssl::stream::connect(const std::string &host, unsigned short port) {
    if (!defaultContext)
        co_return tl::unexpected(defaultContext.error());

    co_return co_await connect(*defaultContext, host, port);
}

zero::async::coroutine::Task<std::shared_ptr<asyncio::net::stream::IBuffer>, std::error_code>
asyncio::net::ssl::stream::connect(const std::shared_ptr<Context> &context, const asyncio::net::Address &address) {
    tl::expected<std::shared_ptr<asyncio::net::stream::IBuffer>, std::error_code> result;

    switch (address.index()) {
        case 0: {
            IPv4Address ipv4 = std::get<IPv4Address>(address);
            result = co_await connect(context, zero::os::net::stringify(ipv4.ip), ipv4.port);
            break;
        }

        case 1: {
            IPv6Address ipv6 = std::get<IPv6Address>(address);
            result = co_await connect(context, zero::os::net::stringify(ipv6.ip), ipv6.port);
            break;
        }

        default:
            result = tl::unexpected(make_error_code(std::errc::address_family_not_supported));
            break;
    }

    co_return result;
}

zero::async::coroutine::Task<std::shared_ptr<asyncio::net::stream::IBuffer>, std::error_code>
asyncio::net::ssl::stream::connect(const std::shared_ptr<Context> &context, std::span<const Address> addresses) {
    if (addresses.empty())
        co_return tl::unexpected(make_error_code(std::errc::invalid_argument));

    auto it = addresses.begin();

    while (true) {
        auto result = co_await connect(context, *it);

        if (result)
            co_return result;

        if (it == addresses.end())
            co_return tl::unexpected(result.error());

        it++;
    }
}

zero::async::coroutine::Task<std::shared_ptr<asyncio::net::stream::IBuffer>, std::error_code>
asyncio::net::ssl::stream::connect(
        const std::shared_ptr<Context> &context,
        const std::string &host,
        unsigned short port
) {
    SSL *ssl = SSL_new(context.get());

    if (!ssl)
        co_return tl::unexpected(make_error_code((Error) ERR_get_error()));

    SSL_set_tlsext_host_name(ssl, host.c_str());
    SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);

    if (!SSL_set1_host(ssl, host.c_str())) {
        SSL_free(ssl);
        co_return tl::unexpected(make_error_code((Error) ERR_get_error()));
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

    bufferevent_setcb(
            bev,
            nullptr,
            nullptr,
            [](bufferevent *bev, short what, void *arg) {
                auto promise = static_cast<zero::async::promise::Promise<void, std::error_code> *>(arg);

                if ((what & BEV_EVENT_CONNECTED) == 0) {
                    unsigned long e = bufferevent_get_openssl_error(bev);
                    promise->reject(
                            e ?
                            make_error_code((Error) e) :
                            std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category())
                    );
                    return;
                }

                promise->resolve();
            },
            &promise
    );

    if (bufferevent_socket_connect_hostname(bev, getEventLoop()->dnsBase(), AF_UNSPEC, host.c_str(), port) < 0) {
        bufferevent_free(bev);
        co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }

    auto result = co_await promise;

    if (!result) {
        bufferevent_free(bev);
        co_return tl::unexpected(result.error());
    }

    co_return std::make_shared<Buffer>(bev);
}

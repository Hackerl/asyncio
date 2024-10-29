#include <asyncio/net/tls.h>
#include <asyncio/fs.h>

#ifdef _WIN32
std::expected<void, std::error_code> loadSystemCerts(X509_STORE *store, const std::string &name) {
    const auto systemStore = CertOpenSystemStoreA(0, name.c_str());

    if (!systemStore)
        return std::unexpected{std::error_code{static_cast<int>(GetLastError()), std::system_category()}};

    DEFER(CertCloseStore(systemStore, 0));

    PCCERT_CONTEXT ctx{};

    DEFER(
        if (ctx)
            CertFreeCertificateContext(ctx);
    );

    while (true) {
        ctx = CertEnumCertificatesInStore(systemStore, ctx);

        if (!ctx)
            return {};

        const auto cert = d2i_X509(
            nullptr,
            const_cast<const unsigned char **>(&ctx->pbCertEncoded),
            static_cast<long>(ctx->cbCertEncoded)
        );

        if (!cert)
            return std::unexpected{asyncio::net::tls::openSSLError()};

        DEFER(X509_free(cert));
        EXPECT(asyncio::net::tls::expected([&] {
            return X509_STORE_add_cert(store, cert);
        }));
    }
}
#endif

std::error_code asyncio::net::tls::openSSLError() {
    return static_cast<OpenSSLError>(ERR_get_error());
}

std::expected<asyncio::net::tls::Certificate, std::error_code>
asyncio::net::tls::Certificate::load(const std::string_view content) {
    const std::unique_ptr<BIO, decltype(&BIO_free)> bio{
        BIO_new_mem_buf(content.data(), static_cast<int>(content.length())),
        BIO_free
    };

    if (!bio)
        return std::unexpected{openSSLError()};

    std::shared_ptr<X509> cert{
        PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr),
        X509_free
    };

    if (!cert)
        return std::unexpected{openSSLError()};

    return Certificate{std::move(cert)};
}

asyncio::task::Task<asyncio::net::tls::Certificate, std::error_code>
asyncio::net::tls::Certificate::loadFile(const std::filesystem::path &path) {
    co_return co_await fs::readString(path).andThen(load);
}

std::expected<asyncio::net::tls::PrivateKey, std::error_code>
asyncio::net::tls::PrivateKey::load(const std::string_view content) {
    const std::unique_ptr<BIO, decltype(&BIO_free)> bio{
        BIO_new_mem_buf(content.data(), static_cast<int>(content.length())),
        BIO_free
    };

    if (!bio)
        return std::unexpected{openSSLError()};

    std::shared_ptr<EVP_PKEY> key{
        PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr),
        EVP_PKEY_free
    };

    if (!key)
        return std::unexpected{openSSLError()};

    return PrivateKey{std::move(key)};
}

asyncio::task::Task<asyncio::net::tls::PrivateKey, std::error_code>
asyncio::net::tls::PrivateKey::loadFile(const std::filesystem::path &path) {
    co_return co_await fs::readString(path).andThen(load);
}

template<typename T>
T &asyncio::net::tls::Config<T>::minVersion(const Version version) {
    mMinVersion = version;
    return *static_cast<T *>(this);
}

template<typename T>
T &asyncio::net::tls::Config<T>::maxVersion(const Version version) {
    mMaxVersion = version;
    return *static_cast<T *>(this);
}

template<typename T>
T &asyncio::net::tls::Config<T>::rootCAs(std::list<Certificate> certificates) {
    mRootCAs = std::move(certificates);
    return *static_cast<T *>(this);
}

template<typename T>
T &asyncio::net::tls::Config<T>::certKeyPairs(std::list<CertKeyPair> pairs) {
    mCertKeyPairs = std::move(pairs);
    return *static_cast<T *>(this);
}

template<typename T>
std::expected<asyncio::net::tls::Context, std::error_code> asyncio::net::tls::Config<T>::build() const {
    Context context{SSL_CTX_new(TLS_method()), SSL_CTX_free};

    if (!context)
        return std::unexpected{openSSLError()};

    EXPECT(expected([&] {
        return SSL_CTX_set_min_proto_version(context.get(), std::to_underlying(mMinVersion));
    }));

    EXPECT(expected([&] {
        return SSL_CTX_set_max_proto_version(context.get(), std::to_underlying(mMaxVersion));
    }));

    if (mRootCAs.empty()) {
#ifdef _WIN32
        const auto store = SSL_CTX_get_cert_store(context.get());

        if (!store)
            return std::unexpected{openSSLError()};

        EXPECT(loadSystemCerts(store, "CA"));
        EXPECT(loadSystemCerts(store, "AuthRoot"));
        EXPECT(loadSystemCerts(store, "ROOT"));
#else
        EXPECT(expected([&] {
            return SSL_CTX_set_default_verify_paths(context.get());
        }));
#endif
    } else {
        const auto store = SSL_CTX_get_cert_store(context.get());

        if (!store)
            return std::unexpected{openSSLError()};

        for (const auto &[cert]: mRootCAs) {
            EXPECT(expected([&] {
                return X509_STORE_add_cert(store, cert.get());
            }));
        }
    }

    for (const auto &[cert, key]: mCertKeyPairs) {
        EXPECT(expected([&] {
            return SSL_CTX_use_certificate(context.get(), cert.inner.get());
        }));
        EXPECT(expected([&] {
            return SSL_CTX_use_PrivateKey(context.get(), key.inner.get());
        }));
    }

    if constexpr (std::is_same_v<T, ClientConfig>) {
        SSL_CTX_set_verify(context.get(), mInsecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, nullptr);
    }
    else {
        SSL_CTX_set_verify(
            context.get(),
            mInsecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
            nullptr
        );
    }

    return context;
}

asyncio::net::tls::ClientConfig &asyncio::net::tls::ClientConfig::insecure(const bool enable) {
    mInsecure = enable;
    return *this;
}

asyncio::net::tls::ServerConfig::ServerConfig() : Config{} {
    mInsecure = true;
}

asyncio::net::tls::ServerConfig &asyncio::net::tls::ServerConfig::verifyClient(const bool enable) {
    mInsecure = !enable;
    return *this;
}

template class asyncio::net::tls::Config<asyncio::net::tls::ClientConfig>;
template class asyncio::net::tls::Config<asyncio::net::tls::ServerConfig>;

DEFINE_ERROR_CATEGORY_INSTANCES(asyncio::net::tls::OpenSSLError, asyncio::net::tls::TLSError)

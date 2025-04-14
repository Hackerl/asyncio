#include <asyncio/net/tls.h>
#include <asyncio/fs.h>

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

#ifdef _WIN32
std::expected<void, std::error_code>
asyncio::net::tls::Config::loadSystemCerts(X509_STORE *store, const std::string &name) {
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
            return std::unexpected{openSSLError()};

        DEFER(X509_free(cert));
        EXPECT(asyncio::net::tls::expected([&] {
            return X509_STORE_add_cert(store, cert);
        }));
    }
}
#endif

asyncio::net::tls::ServerConfig::ServerConfig() : Config{} {
    mInsecure = true;
}

DEFINE_ERROR_CATEGORY_INSTANCES(asyncio::net::tls::OpenSSLError, asyncio::net::tls::TLSError)

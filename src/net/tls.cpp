#include <asyncio/net/tls.h>
#include <asyncio/fs.h>

#ifdef _WIN32
#include <zero/os/windows/error.h>
#endif

#ifdef ASYNCIO_EMBED_CA_CERT
#include <ca_cert.h>
#include <openssl/x509.h>
#endif

#ifdef __linux__
constexpr auto SYSTEM_CA_BUNDLE_PATHS = {
    "/etc/ssl/certs/ca-certificates.crt",
    "/etc/pki/tls/certs/ca-bundle.crt",
    "/etc/ssl/ca-bundle.pem",
    "/etc/pki/tls/cacert.pem",
    "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
    "/etc/ssl/cert.pem"
};
#endif

std::error_code asyncio::net::tls::openSSLError() {
    return static_cast<OpenSSLError>(ERR_get_error());
}

#ifdef __linux__
std::optional<std::filesystem::path> asyncio::net::tls::systemCABundle() {
    const auto it = std::ranges::find_if(
        SYSTEM_CA_BUNDLE_PATHS,
        [](const auto &path) {
            return zero::filesystem::exists(path).value_or(false);
        }
    );

    if (it == SYSTEM_CA_BUNDLE_PATHS.end())
        return std::nullopt;

    return *it;
}
#endif

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

#ifdef ASYNCIO_EMBED_CA_CERT
std::expected<void, std::error_code> asyncio::net::tls::Config::loadEmbeddedCA(X509_STORE *store) {
    const std::unique_ptr<BIO, decltype(&BIO_free)> bio{
        BIO_new_mem_buf(CA_CERT.data(), CA_CERT.size()),
        BIO_free
    };

    if (!bio)
        return std::unexpected{openSSLError()};

    const auto info = PEM_X509_INFO_read_bio(bio.get(), nullptr, nullptr, nullptr);

    if (!info)
        return std::unexpected{openSSLError()};

    Z_DEFER(sk_X509_INFO_pop_free(info, X509_INFO_free));

    for (int i{0}; i < sk_X509_INFO_num(info); ++i) {
        const auto *item = sk_X509_INFO_value(info, i);

        if (item->x509) {
            Z_EXPECT(asyncio::net::tls::expected([&] {
                return X509_STORE_add_cert(store, item->x509);
            }));
        }

        if (item->crl) {
            Z_EXPECT(asyncio::net::tls::expected([&] {
                return X509_STORE_add_crl(store, item->crl);
            }));
        }
    }

    return {};
}
#endif

#ifdef _WIN32
std::expected<void, std::error_code>
asyncio::net::tls::Config::loadSystemCerts(X509_STORE *store, const std::string &name) {
    const auto systemStore = CertOpenSystemStoreA(0, name.c_str());

    if (!systemStore)
        return std::unexpected{std::error_code{static_cast<int>(GetLastError()), std::system_category()}};

    Z_DEFER(zero::error::guard(zero::os::windows::expected([&] {
        return CertCloseStore(systemStore, 0);
    })));

    PCCERT_CONTEXT ctx{};

    Z_DEFER(
        if (ctx) {
            zero::error::guard(zero::os::windows::expected([&] {
                return CertFreeCertificateContext(ctx);
            }));
        }
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

        Z_DEFER(X509_free(cert));
        Z_EXPECT(asyncio::net::tls::expected([&] {
            return X509_STORE_add_cert(store, cert);
        }));
    }
}
#endif

asyncio::net::tls::ServerConfig::ServerConfig() : Config{} {
    mInsecure = true;
}

Z_DEFINE_ERROR_CATEGORY_INSTANCES(asyncio::net::tls::OpenSSLError, asyncio::net::tls::TLSError)

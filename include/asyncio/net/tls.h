#ifndef ASYNCIO_TLS_H
#define ASYNCIO_TLS_H

#include <filesystem>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <zero/defer.h>
#include <asyncio/io.h>
#include <asyncio/sync/mutex.h>

namespace asyncio::net::tls {
    Z_DEFINE_ERROR_TRANSFORMER(
        OpenSSLError,
        "asyncio::net::tls::openssl",
        ([](const int value) -> std::string {
            std::array<char, 1024> buffer{};
            ERR_error_string_n(static_cast<unsigned long>(value), buffer.data(), buffer.size());
            return buffer.data();
        })
    )

    std::error_code openSSLError();

#ifdef __linux__
    std::optional<std::filesystem::path> systemCABundle();
#endif

    template<typename F>
        requires (std::is_same_v<std::invoke_result_t<F>, int> || std::is_same_v<std::invoke_result_t<F>, long>)
    std::expected<void, std::error_code> expected(F &&f) {
        const auto result = f();

        if (result != 1)
            return std::unexpected{openSSLError()};

        return {};
    }

    enum class Version {
        TLS_VERSION_1 = TLS1_VERSION,
        TLS_VERSION_1_1 = TLS1_1_VERSION,
        TLS_VERSION_1_2 = TLS1_2_VERSION,
        TLS_VERSION_1_3 = TLS1_3_VERSION,
        TLS_VERSION_3 = SSL3_VERSION
    };

    struct Certificate {
        std::shared_ptr<X509> inner;

        static std::expected<Certificate, std::error_code> load(std::string_view content);
        static task::Task<Certificate, std::error_code> loadFile(const std::filesystem::path &path);
    };

    struct PrivateKey {
        std::shared_ptr<EVP_PKEY> inner;

        static std::expected<PrivateKey, std::error_code> load(std::string_view content);
        static task::Task<PrivateKey, std::error_code> loadFile(const std::filesystem::path &path);
    };

    struct CertKeyPair {
        Certificate cert;
        PrivateKey key;
    };

    using Context = std::shared_ptr<SSL_CTX>;

    class ClientConfig;
    class ServerConfig;

    class Config {
#ifdef _WIN32
        static std::expected<void, std::error_code> loadSystemCerts(X509_STORE *store, const std::string &name);
#endif

    public:
        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&minVersion(this Self &&self, const Version version) {
            self.mMinVersion = version;
            return std::forward<Self>(self);
        }

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&maxVersion(this Self &&self, const Version version) {
            self.mMaxVersion = version;
            return std::forward<Self>(self);
        }

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&rootCAs(this Self &&self, std::list<Certificate> certificates) {
            self.mRootCAs = std::move(certificates);
            return std::forward<Self>(self);
        }

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&certKeyPairs(this Self &&self, std::list<CertKeyPair> pairs) {
            self.mCertKeyPairs = std::move(pairs);
            return std::forward<Self>(self);
        }

        template<typename Self>
        std::expected<Context, std::error_code> build(this const Self &self) {
            Context context{SSL_CTX_new(TLS_method()), SSL_CTX_free};

            if (!context)
                return std::unexpected{openSSLError()};

            Z_EXPECT(expected([&] {
                return SSL_CTX_set_min_proto_version(context.get(), std::to_underlying(self.mMinVersion));
            }));

            Z_EXPECT(expected([&] {
                return SSL_CTX_set_max_proto_version(context.get(), std::to_underlying(self.mMaxVersion));
            }));

            if (self.mRootCAs.empty()) {
#ifdef _WIN32
                const auto store = SSL_CTX_get_cert_store(context.get());

                if (!store)
                    return std::unexpected{openSSLError()};

                Z_EXPECT(loadSystemCerts(store, "CA"));
                Z_EXPECT(loadSystemCerts(store, "AuthRoot"));
                Z_EXPECT(loadSystemCerts(store, "ROOT"));
#else
                Z_EXPECT(expected([&] {
                    return SSL_CTX_set_default_verify_paths(context.get());
                }));
#ifdef __linux__
                if (const auto bundle = systemCABundle()) {
                    Z_EXPECT(expected([&] {
                        return SSL_CTX_load_verify_locations(context.get(), bundle->c_str(), nullptr);
                    }));
                }
#endif
#endif
            } else {
                const auto store = SSL_CTX_get_cert_store(context.get());

                if (!store)
                    return std::unexpected{openSSLError()};

                for (const auto &[cert]: self.mRootCAs) {
                    Z_EXPECT(expected([&] {
                        return X509_STORE_add_cert(store, cert.get());
                    }));
                }
            }

            for (const auto &[cert, key]: self.mCertKeyPairs) {
                Z_EXPECT(expected([&] {
                    return SSL_CTX_use_certificate(context.get(), cert.inner.get());
                }));
                Z_EXPECT(expected([&] {
                    return SSL_CTX_use_PrivateKey(context.get(), key.inner.get());
                }));
            }

            if constexpr (std::is_same_v<Self, ClientConfig>) {
                SSL_CTX_set_verify(context.get(), self.mInsecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, nullptr);
            }
            else {
                SSL_CTX_set_verify(
                    context.get(),
                    self.mInsecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                    nullptr
                );
            }

            return context;
        }

    protected:
        Version mMinVersion{Version::TLS_VERSION_1_2};
        Version mMaxVersion{Version::TLS_VERSION_1_3};
        bool mInsecure{false};
        std::list<Certificate> mRootCAs;
        std::list<CertKeyPair> mCertKeyPairs;
    };

    class ClientConfig : public Config {
    public:
        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&insecure(this Self &&self, const bool enable) {
            self.mInsecure = enable;
            return std::forward<Self>(self);
        }
    };

    class ServerConfig : public Config {
    public:
        ServerConfig();

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&verifyClient(this Self &&self, const bool enable) {
            self.mInsecure = !enable;
            return std::forward<Self>(self);
        }
    };

    Z_DEFINE_ERROR_CODE_EX(
        TLSError,
        "asyncio::net::tls",
        UNEXPECTED_EOF, "Unexpected end of file", IOError::UNEXPECTED_EOF
    )

    template<typename T>
        requires (
            zero::detail::Trait<T, IReader> &&
            zero::detail::Trait<T, IWriter> &&
            zero::detail::Trait<T, ICloseable>
        )
    class TLS final : public IReader, public IWriter, public ICloseable, public IHalfCloseable {
    public:
        TLS(T stream, std::unique_ptr<SSL, decltype(&SSL_free)> ssl)
            : mStream{std::move(stream)}, mSSL{std::move(ssl)} {
        }

    private:
        task::Task<void, std::error_code> transferIn() {
            auto &mutex = mMutexes[0];
            const auto locked = mutex.locked();

            Z_CO_EXPECT(co_await mutex.lock());
            Z_DEFER(mutex.unlock());

            if (locked)
                co_return {};

            std::array<std::byte, 10240> data; // NOLINT(*-pro-type-member-init)
            const auto n = co_await std::invoke(&IReader::read, mStream, data);
            Z_CO_EXPECT(n);

            if (*n == 0)
                co_return std::unexpected{make_error_code(TLSError::UNEXPECTED_EOF)};

            const auto result = BIO_write(SSL_get_rbio(mSSL.get()), data.data(), static_cast<int>(*n));
            assert(result == *n);
            co_return {};
        }

        task::Task<void, std::error_code> transferOut() {
            auto &mutex = mMutexes[1];

            Z_CO_EXPECT(co_await mutex.lock());
            Z_DEFER(mutex.unlock());

            std::array<std::byte, 10240> data; // NOLINT(*-pro-type-member-init)

            while (true) {
                const auto n = BIO_read(SSL_get_wbio(mSSL.get()), data.data(), data.size());

                if (n <= 0)
                    break;

                Z_CO_EXPECT(co_await std::invoke(
                    &IWriter::writeAll,
                    mStream,
                    std::span{data.data(), static_cast<std::size_t>(n)}
                ));
            }

            co_return {};
        }

    public:
        task::Task<std::size_t, std::error_code> handshake() {
            while (true) {
                const auto result = SSL_do_handshake(mSSL.get());

                if (result == 1) {
                    Z_CO_EXPECT(co_await transferOut());
                    co_return {};
                }

                if (const auto error = SSL_get_error(mSSL.get(), result); error == SSL_ERROR_WANT_READ) {
                    Z_CO_EXPECT(co_await transferOut());
                    Z_CO_EXPECT(co_await transferIn());
                }
                else if (error == SSL_ERROR_WANT_WRITE) {
                    Z_CO_EXPECT(co_await transferOut());
                }
                else if (error == SSL_ERROR_SSL) {
                    co_return std::unexpected{openSSLError()};
                }
                else {
                    co_return std::unexpected{make_error_code(static_cast<OpenSSLError>(error))};
                }
            }
        }

        task::Task<std::size_t, std::error_code> read(const std::span<std::byte> data) override {
            while (true) {
                const auto result = SSL_read(mSSL.get(), data.data(), static_cast<int>(data.size()));

                if (result > 0) {
                    Z_CO_EXPECT(co_await transferOut());
                    co_return result;
                }

                if (result == 0)
                    co_return 0;

                if (const auto error = SSL_get_error(mSSL.get(), result); error == SSL_ERROR_WANT_READ) {
                    Z_CO_EXPECT(co_await transferOut());
                    Z_CO_EXPECT(co_await transferIn());
                }
                else if (error == SSL_ERROR_WANT_WRITE) {
                    Z_CO_EXPECT(co_await transferOut());
                }
                else if (error == SSL_ERROR_SSL) {
                    co_return std::unexpected{openSSLError()};
                }
                else {
                    co_return std::unexpected{make_error_code(static_cast<OpenSSLError>(error))};
                }
            }
        }

        task::Task<std::size_t, std::error_code> write(const std::span<const std::byte> data) override {
            while (true) {
                const auto result = SSL_write(mSSL.get(), data.data(), static_cast<int>(data.size()));

                if (result > 0) {
                    Z_CO_EXPECT(co_await transferOut());
                    co_return result;
                }

                if (const auto error = SSL_get_error(mSSL.get(), result); error == SSL_ERROR_WANT_READ) {
                    Z_CO_EXPECT(co_await transferOut());
                    Z_CO_EXPECT(co_await transferIn());
                }
                else if (error == SSL_ERROR_WANT_WRITE) {
                    Z_CO_EXPECT(co_await transferOut());
                }
                else if (error == SSL_ERROR_SSL) {
                    co_return std::unexpected{openSSLError()};
                }
                else {
                    co_return std::unexpected{make_error_code(static_cast<OpenSSLError>(error))};
                }
            }
        }

        task::Task<void, std::error_code> shutdown() override {
            while (true) {
                const auto result = SSL_shutdown(mSSL.get());

                if (result == 0 || result == 1)
                    co_return co_await transferOut();

                if (const auto error = SSL_get_error(mSSL.get(), result); error == SSL_ERROR_WANT_READ) {
                    Z_CO_EXPECT(co_await transferOut());
                    Z_CO_EXPECT(co_await transferIn());
                }
                else if (error == SSL_ERROR_WANT_WRITE) {
                    Z_CO_EXPECT(co_await transferOut());
                }
                else if (error == SSL_ERROR_SSL) {
                    co_return std::unexpected{openSSLError()};
                }
                else {
                    co_return std::unexpected{make_error_code(static_cast<OpenSSLError>(error))};
                }
            }
        }

        task::Task<void, std::error_code> close() override {
            while (true) {
                const auto result = SSL_shutdown(mSSL.get());

                if (result == 0) {
                    Z_CO_EXPECT(co_await transferOut());
                    continue;
                }

                if (result == 1) {
                    Z_CO_EXPECT(co_await transferOut());
                    break;
                }

                if (const auto error = SSL_get_error(mSSL.get(), result); error == SSL_ERROR_WANT_READ) {
                    Z_CO_EXPECT(co_await transferOut());
                    Z_CO_EXPECT(co_await transferIn());
                }
                else if (error == SSL_ERROR_WANT_WRITE) {
                    Z_CO_EXPECT(co_await transferOut());
                }
                else if (error == SSL_ERROR_SSL) {
                    co_return std::unexpected{openSSLError()};
                }
                else {
                    co_return std::unexpected{make_error_code(static_cast<OpenSSLError>(error))};
                }
            }

            Z_CO_EXPECT(co_await std::invoke(&ICloseable::close, mStream));
            co_return {};
        }

    private:
        T mStream;
        std::unique_ptr<SSL, decltype(&SSL_free)> mSSL;
        std::array<sync::Mutex, 2> mMutexes;
    };

    template<typename T>
        requires (
            zero::detail::Trait<T, IReader> &&
            zero::detail::Trait<T, IWriter> &&
            zero::detail::Trait<T, ICloseable>
        )
    task::Task<TLS<T>, std::error_code> connect(
        T stream,
        const Context context,
        const std::optional<std::string> serverName = std::nullopt
    ) {
        std::unique_ptr<SSL, decltype(&SSL_free)> ssl{SSL_new(context.get()), SSL_free};

        if (!ssl)
            co_return std::unexpected{openSSLError()};

        if (serverName) {
            SSL_set_tlsext_host_name(ssl.get(), serverName->c_str());
            SSL_set_hostflags(ssl.get(), X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);

            Z_CO_EXPECT(expected([&] {
                return SSL_set1_host(ssl.get(), serverName->c_str());
            }));
        }

        std::unique_ptr<BIO, decltype(&BIO_free)> readBIO{BIO_new(BIO_s_mem()), BIO_free};

        if (!readBIO)
            co_return std::unexpected{openSSLError()};

        std::unique_ptr<BIO, decltype(&BIO_free)> writeBIO{BIO_new(BIO_s_mem()), BIO_free};

        if (!writeBIO)
            co_return std::unexpected{openSSLError()};

        SSL_set_bio(ssl.get(), readBIO.release(), writeBIO.release());
        SSL_set_connect_state(ssl.get());

        TLS tls{std::move(stream), std::move(ssl)};
        Z_CO_EXPECT(co_await tls.handshake());
        co_return tls;
    }

    template<typename T>
        requires (
            zero::detail::Trait<T, IReader> &&
            zero::detail::Trait<T, IWriter> &&
            zero::detail::Trait<T, ICloseable>
        )
    task::Task<TLS<T>, std::error_code> accept(T stream, const Context context) {
        std::unique_ptr<SSL, decltype(&SSL_free)> ssl{SSL_new(context.get()), SSL_free};

        if (!ssl)
            co_return std::unexpected{openSSLError()};

        std::unique_ptr<BIO, decltype(&BIO_free)> readBIO{BIO_new(BIO_s_mem()), BIO_free};

        if (!readBIO)
            co_return std::unexpected{openSSLError()};

        std::unique_ptr<BIO, decltype(&BIO_free)> writeBIO{BIO_new(BIO_s_mem()), BIO_free};

        if (!writeBIO)
            co_return std::unexpected{openSSLError()};

        SSL_set_bio(ssl.get(), readBIO.release(), writeBIO.release());
        SSL_set_accept_state(ssl.get());

        TLS tls{std::move(stream), std::move(ssl)};
        Z_CO_EXPECT(co_await tls.handshake());
        co_return tls;
    }
}

Z_DECLARE_ERROR_CODES(asyncio::net::tls::OpenSSLError, asyncio::net::tls::TLSError)

#endif //ASYNCIO_TLS_H

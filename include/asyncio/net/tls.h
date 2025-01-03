#ifndef ASYNCIO_TLS_H
#define ASYNCIO_TLS_H

#include <variant>
#include <filesystem>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <zero/defer.h>
#include <asyncio/io.h>
#include <asyncio/sync/mutex.h>

namespace asyncio::net::tls {
    DEFINE_ERROR_TRANSFORMER(
        OpenSSLError,
        "asyncio::net::tls::openssl",
        ([](const int value) -> std::string {
            std::array<char, 1024> buffer{};
            ERR_error_string_n(static_cast<unsigned long>(value), buffer.data(), buffer.size());
            return buffer.data();
        })
    )

    std::error_code openSSLError();

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

    template<typename T>
    class Config {
        static_assert(std::is_same_v<T, ClientConfig> || std::is_same_v<T, ServerConfig>);

    public:
        T &minVersion(Version version);
        T &maxVersion(Version version);
        T &rootCAs(std::list<Certificate> certificates);
        T &certKeyPairs(std::list<CertKeyPair> pairs);

        [[nodiscard]] std::expected<Context, std::error_code> build() const;

    protected:
        Version mMinVersion{Version::TLS_VERSION_1_2};
        Version mMaxVersion{Version::TLS_VERSION_1_3};
        bool mInsecure{false};
        std::list<Certificate> mRootCAs;
        std::list<CertKeyPair> mCertKeyPairs;
    };

    class ClientConfig : public Config<ClientConfig> {
    public:
        ClientConfig &insecure(bool enable);
    };

    class ServerConfig : public Config<ServerConfig> {
    public:
        ServerConfig();
        ServerConfig &verifyClient(bool enable);
    };

    DEFINE_ERROR_CODE_EX(
        TLSError,
        "asyncio::net::tls",
        UNEXPECTED_EOF, "unexpected end of file", IOError::UNEXPECTED_EOF
    )

    template<typename T>
        requires (Trait<T, IReader> && Trait<T, IWriter> && Trait<T, ICloseable>)
    class TLS final : public IReader, public IWriter, public ICloseable {
    public:
        TLS(T stream, std::unique_ptr<SSL, decltype(&SSL_free)> ssl)
            : mStream{std::move(stream)}, mSSL{std::move(ssl)} {
        }

    private:
        task::Task<void, std::error_code> transferIn() {
            auto &mutex = mMutexes[0];
            const auto locked = mutex.locked();

            CO_EXPECT(co_await mutex.lock());
            DEFER(mutex.unlock());

            if (locked)
                co_return {};

            std::array<std::byte, 10240> data; // NOLINT(*-pro-type-member-init)
            const auto n = co_await std::invoke(&IReader::read, mStream, data);
            CO_EXPECT(n);

            if (*n == 0)
                co_return std::unexpected{make_error_code(TLSError::UNEXPECTED_EOF)};

            const auto result = BIO_write(SSL_get_rbio(mSSL.get()), data.data(), static_cast<int>(*n));
            assert(result == *n);
            co_return {};
        }

        task::Task<void, std::error_code> transferOut() {
            auto &mutex = mMutexes[1];

            CO_EXPECT(co_await mutex.lock());
            DEFER(mutex.unlock());

            std::array<std::byte, 10240> data; // NOLINT(*-pro-type-member-init)

            while (true) {
                const auto n = BIO_read(SSL_get_wbio(mSSL.get()), data.data(), data.size());

                if (n <= 0)
                    break;

                CO_EXPECT(co_await std::invoke(
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
                    CO_EXPECT(co_await transferOut());
                    co_return {};
                }

                if (const auto error = SSL_get_error(mSSL.get(), result); error == SSL_ERROR_WANT_READ) {
                    CO_EXPECT(co_await transferOut());
                    CO_EXPECT(co_await transferIn());
                }
                else if (error == SSL_ERROR_WANT_WRITE) {
                    CO_EXPECT(co_await transferOut());
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
                    CO_EXPECT(co_await transferOut());
                    co_return result;
                }

                if (result == 0) {
                    if (const auto res = SSL_shutdown(mSSL.get()); res != 1) {
                        assert(res < 0);
                        co_return std::unexpected{
                            make_error_code(static_cast<OpenSSLError>(SSL_get_error(mSSL.get(), result)))
                        };
                    }

                    CO_EXPECT(co_await transferOut());
                    co_return 0;
                }

                if (const auto error = SSL_get_error(mSSL.get(), result); error == SSL_ERROR_WANT_READ) {
                    CO_EXPECT(co_await transferOut());
                    CO_EXPECT(co_await transferIn());
                }
                else if (error == SSL_ERROR_WANT_WRITE) {
                    CO_EXPECT(co_await transferOut());
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
                    CO_EXPECT(co_await transferOut());
                    co_return result;
                }

                if (const auto error = SSL_get_error(mSSL.get(), result); error == SSL_ERROR_WANT_READ) {
                    CO_EXPECT(co_await transferOut());
                    CO_EXPECT(co_await transferIn());
                }
                else if (error == SSL_ERROR_WANT_WRITE) {
                    CO_EXPECT(co_await transferOut());
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
                    CO_EXPECT(co_await transferOut());
                    continue;
                }

                if (result == 1) {
                    CO_EXPECT(co_await transferOut());
                    break;
                }

                if (const auto error = SSL_get_error(mSSL.get(), result); error == SSL_ERROR_WANT_READ) {
                    CO_EXPECT(co_await transferOut());
                    CO_EXPECT(co_await transferIn());
                }
                else if (error == SSL_ERROR_WANT_WRITE) {
                    CO_EXPECT(co_await transferOut());
                }
                else if (error == SSL_ERROR_SSL) {
                    co_return std::unexpected{openSSLError()};
                }
                else {
                    co_return std::unexpected{make_error_code(static_cast<OpenSSLError>(error))};
                }
            }

            CO_EXPECT(co_await std::invoke(&ICloseable::close, mStream));
            co_return {};
        }

    private:
        T mStream;
        std::unique_ptr<SSL, decltype(&SSL_free)> mSSL;
        std::array<sync::Mutex, 2> mMutexes;
    };

    template<typename T>
        requires (Trait<T, IReader> && Trait<T, IWriter> && Trait<T, ICloseable>)
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

            CO_EXPECT(expected([&] {
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
        CO_EXPECT(co_await tls.handshake());
        co_return tls;
    }

    template<typename T>
        requires (Trait<T, IReader> && Trait<T, IWriter> && Trait<T, ICloseable>)
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
        CO_EXPECT(co_await tls.handshake());
        co_return tls;
    }
}

DECLARE_ERROR_CODES(asyncio::net::tls::OpenSSLError, asyncio::net::tls::TLSError)

#endif //ASYNCIO_TLS_H

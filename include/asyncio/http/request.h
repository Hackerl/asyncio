#ifndef ASYNCIO_REQUEST_H
#define ASYNCIO_REQUEST_H

#include "url.h"
#include <map>
#include <variant>
#include <asyncio/io.h>
#include <nlohmann/json.hpp>

namespace asyncio::http {
    Z_DEFINE_ERROR_TRANSFORMER(
        CURLError,
        "asyncio::http::curl",
        [](const int value) { return curl_easy_strerror(static_cast<CURLcode>(value)); }
    )

    Z_DEFINE_ERROR_TRANSFORMER(
        CURLMError,
        "asyncio::http::curl::multi",
        [](const int value) { return curl_multi_strerror(static_cast<CURLMcode>(value)); }
    )

    template<typename F>
        requires std::is_same_v<std::invoke_result_t<F>, CURLcode>
    std::expected<void, std::error_code> expected(F &&f) {
        if (const auto code = f(); code != CURLE_OK)
            return std::unexpected{make_error_code(static_cast<CURLError>(code))};

        return {};
    }

    template<typename F>
        requires std::is_same_v<std::invoke_result_t<F>, CURLMcode>
    std::expected<void, std::error_code> expected(F &&f) {
        if (const auto code = f(); code != CURLM_OK)
            return std::unexpected{make_error_code(static_cast<CURLMError>(code))};

        return {};
    }

    struct Connection {
        struct UpstreamContext {
            std::vector<std::byte> data;
            std::shared_ptr<IReader> reader;
            std::optional<task::Task<std::size_t, std::error_code>> task;
            std::optional<zero::async::promise::Future<std::size_t, std::error_code>> future;
            bool aborted{};
        };

        struct DownstreamContext {
            std::size_t skip{};
            std::span<std::byte> data;
            std::optional<Promise<std::size_t, std::error_code>> promise;
        };

        ~Connection() {
            for (const auto &defer: defers)
                defer();
        }

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy;
        bool finished{};
        bool transferring{};
        Promise<void, std::error_code> promise;
        UpstreamContext upstream;
        DownstreamContext downstream;
        std::list<std::function<void()>> defers;
        std::optional<std::error_code> error;
    };

    class Requests;

    class Response final : public IReader {
    public:
        Response(Requests *requests, std::shared_ptr<Connection> connection);
        Response(Response &&) = default;
        Response &operator=(Response &&rhs) noexcept = default;
        ~Response() override;

        [[nodiscard]] long statusCode() const;
        [[nodiscard]] std::optional<std::uint64_t> contentLength() const;
        [[nodiscard]] std::optional<std::string> contentType() const;
        [[nodiscard]] std::vector<std::string> cookies() const;
        [[nodiscard]] std::optional<std::string> header(const std::string &name) const;

        task::Task<std::string, std::error_code> string();
        task::Task<void, std::error_code> output(std::filesystem::path path);

        task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;

    private:
        Requests *mRequests;
        std::shared_ptr<Connection> mConnection;
    };

    struct TLSConfig {
        bool insecure{};
        std::optional<std::filesystem::path> ca;
        std::optional<std::filesystem::path> cert;
        std::optional<std::filesystem::path> privateKey;
        std::optional<std::string> password;
    };

    struct Options {
        std::optional<std::string> proxy;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> cookies;
        std::optional<std::chrono::seconds> timeout;
        std::optional<std::chrono::seconds> connectTimeout;
        std::optional<std::string> userAgent;
        TLSConfig tls;
        std::list<std::function<std::expected<void, std::error_code>(Connection &)>> hooks;
    };

    class Requests {
        struct Core {
            struct Context {
                uv::Handle<uv_poll_t> poll;
                Core *core{};
                curl_socket_t s{};
            };

            ~Core();

            int running{};
            Options options;
            uv::Handle<uv_timer_t> timer;
            CURLM *multi;

            void recycle();
            std::expected<void, std::error_code> setTimer(long ms);
            std::expected<void, std::error_code> handle(curl_socket_t s, int action, Context *context);
        };

    public:
        explicit Requests(std::unique_ptr<Core> core);
        static std::expected<Requests, std::error_code> make(Options options = {});

    private:
        static std::size_t onRead(char *buffer, std::size_t size, std::size_t n, void *userdata);
        static std::size_t onWrite(const char *buffer, std::size_t size, std::size_t n, void *userdata);

        std::expected<std::shared_ptr<Connection>, std::error_code>
        prepare(std::string method, const URL &url, const std::optional<Options> &options);

        task::Task<Response, std::error_code>
        perform(std::shared_ptr<Connection> connection);

    public:
        Options &options();
        [[nodiscard]] const Options &options() const;

        task::Task<Response, std::error_code>
        request(std::string method, URL url, std::optional<Options> options);

        task::Task<Response, std::error_code>
        request(std::string method, URL url, std::optional<Options> options, std::string payload);

        task::Task<Response, std::error_code> request(
            std::string method,
            URL url,
            std::optional<Options> options,
            std::map<std::string, std::string> payload
        );

        task::Task<Response, std::error_code> request(
            std::string method,
            URL url,
            std::optional<Options> options,
            std::map<std::string, std::variant<std::string, std::filesystem::path>> payload
        );

        template<typename T>
            requires (
                std::is_class_v<T> &&
                !std::is_same_v<T, std::string> &&
                (std::is_same_v<T, nlohmann::json> || nlohmann::detail::is_compatible_type<nlohmann::json, T>::value)
            )
        task::Task<Response, std::error_code> request(
            std::string method,
            const URL url,
            const std::optional<Options> options,
            T payload
        ) {
            auto opt = options.value_or(mCore->options);
            opt.headers["Content-Type"] = "application/json";

            auto connection = prepare(std::move(method), url, std::move(opt));
            Z_CO_EXPECT(connection);

            std::string body;

            if constexpr (std::is_same_v<T, nlohmann::json>)
                body = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            else
                body = nlohmann::json(payload).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

            zero::error::guard(expected([&] {
                return curl_easy_setopt(
                    connection->get()->easy.get(),
                    CURLOPT_COPYPOSTFIELDS,
                    body.c_str()
                );
            }));

            co_return co_await perform(*std::move(connection));
        }

        template<zero::detail::Trait<IReader> T>
        task::Task<Response, std::error_code> request(
            const std::string method,
            const URL url,
            const std::optional<Options> options,
            T payload
        ) {
            auto connection = prepare(method, url, options);
            Z_CO_EXPECT(connection);

            const auto easy = connection.value()->easy.get();

            zero::error::guard(expected([&] {
                return curl_easy_setopt(easy, CURLOPT_UPLOAD, 1L);
            }));

            // `CURLOPT_UPLOAD` will cause the http method to become `PUT`.
            zero::error::guard(expected([&] {
                return curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, method.c_str());
            }));

            zero::error::guard(expected([&] {
                return curl_easy_setopt(easy, CURLOPT_READFUNCTION, onRead);
            }));

            zero::error::guard(expected([&] {
                return curl_easy_setopt(easy, CURLOPT_READDATA, connection->get());
            }));

            if constexpr (zero::detail::Trait<T, ISeekable>) {
                if (const auto length = co_await std::invoke(&ISeekable::length, payload)) {
                    zero::error::guard(expected([&] {
                        return curl_easy_setopt(easy, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(*length));
                    }));
                }
            }

            auto &[data, reader, task, future, aborted] = connection.value()->upstream;

            if constexpr (std::is_convertible_v<std::remove_cvref_t<T>, std::shared_ptr<IReader>>) {
                reader = std::move(payload);
            }
            else {
                reader = std::make_shared<T>(std::move(payload));
            }

            auto response = co_await perform(*connection);

            if (task && !task->done()) {
                aborted = true;
                std::ignore = task->cancel();
                std::ignore = co_await std::move(*future);
            }

            co_return std::move(response);
        }

        task::Task<Response, std::error_code>
        get(URL url, std::optional<Options> options = std::nullopt) {
            return request("GET", std::move(url), std::move(options));
        }

        task::Task<Response, std::error_code>
        head(URL url, std::optional<Options> options = std::nullopt) {
            return request("HEAD", std::move(url), std::move(options));
        }

        task::Task<Response, std::error_code>
        del(URL url, std::optional<Options> options = std::nullopt) {
            return request("DELETE", std::move(url), std::move(options));
        }

        template<typename T>
        task::Task<Response, std::error_code>
        post(URL url, T &&payload, std::optional<Options> options = std::nullopt) {
            return request("POST", std::move(url), std::move(options), std::forward<T>(payload));
        }

        template<typename T>
        task::Task<Response, std::error_code>
        put(URL url, T &&payload, std::optional<Options> options = std::nullopt) {
            return request("PUT", std::move(url), std::move(options), std::forward<T>(payload));
        }

        template<typename T>
        task::Task<Response, std::error_code>
        patch(URL url, T &&payload, std::optional<Options> options = std::nullopt) {
            return request("PATCH", std::move(url), std::move(options), std::forward<T>(payload));
        }

    private:
        std::unique_ptr<Core> mCore;
        friend class Response;
    };
}

Z_DECLARE_ERROR_CODES(asyncio::http::CURLError, asyncio::http::CURLMError)

#endif //ASYNCIO_REQUEST_H

#ifndef ASYNCIO_REQUEST_H
#define ASYNCIO_REQUEST_H

#include "url.h"
#include <map>
#include <variant>
#include <asyncio/io.h>
#include <nlohmann/json.hpp>

namespace asyncio::http {
    struct Connection {
        ~Connection() {
            for (const auto &defer: defers)
                defer();
        }

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy;
        bool finished{};
        bool transferring{};
        std::size_t skip{};
        Promise<void, std::error_code> promise;
        std::span<std::byte> buffer;
        std::optional<Promise<std::size_t, std::error_code>> dataPromise;
        std::list<std::function<void()>> defers;
        std::optional<std::error_code> error;
    };

    class Requests;

    class Response final : public IReader {
    public:
        Response(Requests *requests, std::unique_ptr<Connection> connection);
        Response(Response &&) = default;
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
        std::unique_ptr<Connection> mConnection;
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

            int running{};
            Options options;
            uv::Handle<uv_timer_t> timer;
            std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> multi;

            void recycle();
            std::expected<void, std::error_code> setTimer(long ms);
            std::expected<void, std::error_code> handle(curl_socket_t s, int action, Context *context);
        };

    public:
        DEFINE_ERROR_TRANSFORMER_INNER(
            CURLError,
            "asyncio::http::Requests::curl",
            [](const int value) { return curl_easy_strerror(static_cast<CURLcode>(value)); }
        )

        DEFINE_ERROR_TRANSFORMER_INNER(
            CURLMError,
            "asyncio::http::Requests::curl::multi",
            [](const int value) { return curl_multi_strerror(static_cast<CURLMcode>(value)); }
        )

        explicit Requests(std::unique_ptr<Core> core);
        static std::expected<Requests, std::error_code> make(Options options = {});

        Options &options();
        [[nodiscard]] const Options &options() const;

    private:
        std::expected<std::unique_ptr<Connection>, std::error_code>
        prepare(std::string method, const URL &url, const std::optional<Options> &options);

        task::Task<Response, std::error_code>
        perform(std::unique_ptr<Connection> connection);

    public:
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
            std::map<std::string, std::filesystem::path> payload
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
            CO_EXPECT(connection);

            std::string body;

            if constexpr (std::is_same_v<T, nlohmann::json>)
                body = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            else
                body = nlohmann::json(payload).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

            curl_easy_setopt(
                connection->get()->easy.get(),
                CURLOPT_COPYPOSTFIELDS,
                body.c_str()
            );

            co_return co_await perform(*std::move(connection));
        }

        task::Task<Response, std::error_code>
        get(const URL &url, const std::optional<Options> &options = std::nullopt) {
            return request("GET", url, options);
        }

        task::Task<Response, std::error_code>
        head(const URL &url, const std::optional<Options> &options = std::nullopt) {
            return request("HEAD", url, options);
        }

        task::Task<Response, std::error_code>
        del(const URL &url, const std::optional<Options> &options = std::nullopt) {
            return request("DELETE", url, options);
        }

        template<typename T>
        task::Task<Response, std::error_code>
        post(const URL &url, T &&payload, const std::optional<Options> &options = std::nullopt) {
            return request("POST", url, options, std::forward<T>(payload));
        }

        template<typename T>
        task::Task<Response, std::error_code>
        put(const URL &url, T &&payload, const std::optional<Options> &options = std::nullopt) {
            return request("PUT", url, options, std::forward<T>(payload));
        }

        template<typename T>
        task::Task<Response, std::error_code>
        patch(const URL &url, T &&payload, const std::optional<Options> &options = std::nullopt) {
            return request("PATCH", url, options, std::forward<T>(payload));
        }

    private:
        std::unique_ptr<Core> mCore;
        friend class Response;
    };
}

DECLARE_ERROR_CODES(
    asyncio::http::Requests::CURLError,
    asyncio::http::Requests::CURLMError
)

#endif //ASYNCIO_REQUEST_H

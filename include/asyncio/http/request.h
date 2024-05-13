#ifndef ASYNCIO_REQUEST_H
#define ASYNCIO_REQUEST_H

#include "url.h"
#include <map>
#include <variant>
#include <zero/expect.h>
#include <asyncio/ev/pipe.h>
#include <asyncio/ev/timer.h>
#include <nlohmann/json.hpp>

namespace asyncio::http {
    struct Connection {
        enum class Status {
            NOT_STARTED,
            PAUSED,
            TRANSFERRING
        };

        ~Connection() {
            for (const auto &defer: defers)
                defer();
        }

        std::array<ev::PairedBuffer, 2> buffers;
        std::unique_ptr<CURL, decltype(curl_easy_cleanup) *> easy;
        Promise<void, std::error_code> promise;
        std::list<std::function<void()>> defers;
        std::optional<std::error_code> error;
        Status status;
    };

    class Requests;

    class Response final : public IBufReader, public Reader {
    public:
        DEFINE_ERROR_CODE_TYPES(
            Error,
            "asyncio::http::Response",
            INVALID_JSON, "invalid json message"
        )

        Response(Requests *requests, std::unique_ptr<Connection> connection);
        Response(Response &&) = default;
        ~Response() override;

        [[nodiscard]] long statusCode() const;
        [[nodiscard]] std::optional<curl_off_t> contentLength() const;
        [[nodiscard]] std::optional<std::string> contentType() const;
        [[nodiscard]] std::vector<std::string> cookies() const;
        [[nodiscard]] std::optional<std::string> header(const std::string &name) const;

        [[nodiscard]] zero::async::coroutine::Task<std::string, std::error_code> string() const;
        [[nodiscard]] zero::async::coroutine::Task<void, std::error_code> output(std::filesystem::path path) const;
        [[nodiscard]] zero::async::coroutine::Task<nlohmann::json, std::error_code> json() const;

        template<typename T>
        zero::async::coroutine::Task<T, std::error_code> json() const {
            const auto j = co_await json();
            CO_EXPECT(j);

            try {
                co_return j->template get<T>();
            }
            catch (const nlohmann::json::exception &) {
                co_return tl::unexpected(Error::INVALID_JSON);
            }
        }

        zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        [[nodiscard]] std::size_t capacity() const override;
        [[nodiscard]] std::size_t available() const override;
        zero::async::coroutine::Task<std::string, std::error_code> readLine() override;
        zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> readUntil(std::byte byte) override;
        zero::async::coroutine::Task<void, std::error_code> peek(std::span<std::byte> data) override;

    private:
        Requests *mRequests;
        std::unique_ptr<Connection> mConnection;
    };

    DEFINE_MAKE_ERROR_CODE(Response::Error)

    struct Options {
        std::optional<std::string> proxy;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> cookies;
        std::optional<std::chrono::seconds> timeout;
        std::optional<std::chrono::seconds> connectTimeout;
        std::optional<std::string> userAgent;
    };

    class Requests {
    public:
        DEFINE_ERROR_TRANSFORMER_TYPES(
            CURLError,
            "asyncio::http::Requests::curl",
            [](const int v) { return curl_easy_strerror(static_cast<CURLcode>(v)); }
        )

        DEFINE_ERROR_TRANSFORMER_TYPES(
            CURLMError,
            "asyncio::http::Requests::curl::multi",
            [](const int v) { return curl_multi_strerror(static_cast<CURLMcode>(v)); }
        )

        Requests(CURLM *multi, std::unique_ptr<event, decltype(event_free) *> timer);
        Requests(CURLM *multi, std::unique_ptr<event, decltype(event_free) *> timer, Options options);
        Requests(Requests &&rhs) noexcept;
        Requests &operator=(Requests &&rhs) noexcept;
        ~Requests();

        static tl::expected<Requests, std::error_code> make(const Options &options = {});

    private:
        void onTimer();
        void onCURLTimer(long timeout) const;
        void onCURLEvent(curl_socket_t s, int what, void *data);
        void recycle() const;

    public:
        Options &options();

    private:
        tl::expected<std::unique_ptr<Connection>, std::error_code>
        prepare(std::string method, const URL &url, const std::optional<Options> &options);

        zero::async::coroutine::Task<Response, std::error_code>
        perform(std::unique_ptr<Connection> connection);

    public:
        zero::async::coroutine::Task<Response, std::error_code>
        request(std::string method, URL url, std::optional<Options> options);

        zero::async::coroutine::Task<Response, std::error_code>
        request(std::string method, URL url, std::optional<Options> options, std::string payload);

        zero::async::coroutine::Task<Response, std::error_code> request(
            std::string method,
            URL url,
            std::optional<Options> options,
            std::map<std::string, std::string> payload
        );

        zero::async::coroutine::Task<Response, std::error_code> request(
            std::string method,
            URL url,
            std::optional<Options> options,
            std::map<std::string, std::filesystem::path> payload
        );

        zero::async::coroutine::Task<Response, std::error_code> request(
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
        zero::async::coroutine::Task<Response, std::error_code> request(
            std::string method,
            const URL url,
            const std::optional<Options> options,
            T payload
        ) {
            Options opt = options.value_or(mOptions);
            opt.headers["Content-Type"] = "application/json";

            auto connection = prepare(std::move(method), url, std::move(opt));
            CO_EXPECT(connection);

            std::string body;

            if constexpr (std::is_same_v<T, nlohmann::json>) {
                body = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
            else {
                body = nlohmann::json(payload).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }

            curl_easy_setopt(
                connection->get()->easy.get(),
                CURLOPT_COPYPOSTFIELDS,
                body.c_str()
            );

            co_return co_await perform(*std::move(connection));
        }

        zero::async::coroutine::Task<Response, std::error_code>
        get(const URL &url, const std::optional<Options> &options = std::nullopt) {
            return request("GET", url, options);
        }

        zero::async::coroutine::Task<Response, std::error_code>
        head(const URL &url, const std::optional<Options> &options = std::nullopt) {
            return request("HEAD", url, options);
        }

        zero::async::coroutine::Task<Response, std::error_code>
        del(const URL &url, const std::optional<Options> &options = std::nullopt) {
            return request("DELETE", url, options);
        }

        template<typename T>
        zero::async::coroutine::Task<Response, std::error_code>
        post(const URL &url, T &&payload, const std::optional<Options> &options = std::nullopt) {
            return request("POST", url, options, std::forward<T>(payload));
        }

        template<typename T>
        zero::async::coroutine::Task<Response, std::error_code>
        put(const URL &url, T &&payload, const std::optional<Options> &options = std::nullopt) {
            return request("PUT", url, options, std::forward<T>(payload));
        }

        template<typename T>
        zero::async::coroutine::Task<Response, std::error_code>
        patch(const URL &url, T &&payload, const std::optional<Options> &options = std::nullopt) {
            return request("PATCH", url, options, std::forward<T>(payload));
        }

    private:
        int mRunning;
        Options mOptions;
        std::unique_ptr<event, decltype(event_free) *> mTimer;
        std::unique_ptr<CURLM, decltype(curl_multi_cleanup) *> mMulti;

        friend class Response;
    };

    DEFINE_MAKE_ERROR_CODE(Requests::CURLError)
    DEFINE_MAKE_ERROR_CODE(Requests::CURLMError)
}

DECLARE_ERROR_CODES(
    asyncio::http::Response::Error,
    asyncio::http::Requests::CURLError,
    asyncio::http::Requests::CURLMError
)

#endif //ASYNCIO_REQUEST_H

#ifndef ASYNCIO_REQUEST_H
#define ASYNCIO_REQUEST_H

#include "url.h"
#include <map>
#include <variant>
#include <zero/try.h>
#include <asyncio/ev/pipe.h>
#include <asyncio/ev/timer.h>
#include <nlohmann/json.hpp>

namespace asyncio::http {
    enum CURLError {

    };

    class CURLCategory : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
    };

    const std::error_category &getCURLCategory();
    std::error_code make_error_code(CURLError e);

    enum CURLMError {

    };

    class CURLMCategory : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
    };

    const std::error_category &getCURLMCategory();
    std::error_code make_error_code(CURLMError e);

    struct Connection {
        ~Connection() {
            for (const auto &defer: defers)
                defer();
        }

        std::array<ev::PairedBuffer, 2> buffers;
        std::unique_ptr<CURL, decltype(curl_easy_cleanup) *> easy;
        zero::async::promise::Promise<void, std::error_code> promise;
        std::list<std::function<void(void)>> defers;
        bool transferring;
    };

    class Requests;

    class Response {
    public:
        Response(std::shared_ptr<Requests> requests, std::unique_ptr<Connection> connection);
        Response(Response &&rhs) = default;
        ~Response();

    public:
        long statusCode();
        std::optional<curl_off_t> contentLength();
        std::optional<std::string> contentType();
        std::vector<std::string> cookies();
        std::optional<std::string> header(const std::string &name);

    public:
        zero::async::coroutine::Task<std::string, std::error_code> string();
        zero::async::coroutine::Task<void, std::error_code> output(std::filesystem::path path);
        zero::async::coroutine::Task<nlohmann::json, std::error_code> json();

        template<typename T>
        zero::async::coroutine::Task<T, std::error_code> json() {
            tl::expected<nlohmann::json, std::error_code> j = CO_TRY(co_await json());

            try {
                co_return j->get<T>();
            } catch (const nlohmann::json::exception &) {
                co_return tl::unexpected(make_error_code(std::errc::invalid_argument));
            }
        }

    public:
        ev::IBufferReader &operator*();
        ev::IBufferReader *operator->();

    private:
        std::shared_ptr<Requests> mRequests;
        std::unique_ptr<Connection> mConnection;
    };

    struct Options {
        std::optional<std::string> proxy;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> cookies;
        std::optional<std::chrono::seconds> timeout;
        std::optional<std::string> userAgent;
    };

    class Requests : public std::enable_shared_from_this<Requests> {
    public:
        Requests(CURLM *multi, ev::Timer timer);
        Requests(CURLM *multi, ev::Timer timer, Options options);
        ~Requests();

    private:
        void onCURLTimer(long timeout);
        void onCURLEvent(CURL *easy, curl_socket_t s, int what, void *data);

    private:
        void recycle();

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
                std::string method, URL url,
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

        zero::async::coroutine::Task<Response, std::error_code> request(
                std::string method,
                URL url,
                std::optional<Options> options,
                nlohmann::json payload
        );

        template<typename T>
        requires nlohmann::detail::has_to_json<nlohmann::json, T>::value
        zero::async::coroutine::Task<Response, std::error_code> request(
                std::string method,
                URL url,
                std::optional<Options> options,
                T payload
        ) {
            Options opt = options.value_or(mOptions);
            opt.headers["Content-Type"] = "application/json";

            auto connection = CO_TRY(prepare(std::move(method), std::move(url), std::move(opt)));

            curl_easy_setopt(
                    connection->get()->easy.get(),
                    CURLOPT_COPYPOSTFIELDS,
                    nlohmann::json(payload).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str()
            );

            co_return std::move(co_await perform(std::move(*connection)));
        }

    public:
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
        ev::Timer mTimer;
        std::unique_ptr<CURLM, decltype(curl_multi_cleanup) *> mMulti;

        friend class Response;
    };

    tl::expected<std::shared_ptr<Requests>, std::error_code> makeRequests(const Options &options = {});
}

namespace std {
    template<>
    struct is_error_code_enum<asyncio::http::CURLError> : public true_type {

    };

    template<>
    struct is_error_code_enum<asyncio::http::CURLMError> : public true_type {

    };
}

#endif //ASYNCIO_REQUEST_H

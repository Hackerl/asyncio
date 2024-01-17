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
    enum CURLError {
    };

    class CURLErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
    };

    const std::error_category &curlErrorCategory();
    std::error_code make_error_code(CURLError e);

    enum CURLMError {
    };

    class CURLMErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
    };

    const std::error_category &curlMErrorCategory();
    std::error_code make_error_code(CURLMError e);

    struct Connection {
        enum Status {
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
        zero::async::promise::PromisePtr<void, std::error_code> promise;
        std::list<std::function<void()>> defers;
        std::optional<std::error_code> error;
        Status status;
    };

    class Requests;

    class Response: public IBufReader, public Reader {
    public:
        Response(std::shared_ptr<Requests> requests, std::unique_ptr<Connection> connection);
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
                co_return tl::unexpected(make_error_code(std::errc::bad_message));
            }
        }

        zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        std::size_t capacity() override;
        std::size_t available() override;
        zero::async::coroutine::Task<std::string, std::error_code> readLine() override;
        zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> readUntil(std::byte byte) override;
        zero::async::coroutine::Task<void, std::error_code> peek(std::span<std::byte> data) override;

    private:
        std::shared_ptr<Requests> mRequests;
        std::unique_ptr<Connection> mConnection;
    };

    struct Options {
        std::optional<std::string> proxy;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> cookies;
        std::optional<std::chrono::seconds> timeout;
        std::optional<std::chrono::seconds> connectTimeout;
        std::optional<std::string> userAgent;
    };

    class Requests : public std::enable_shared_from_this<Requests> {
    public:
        Requests(CURLM *multi, ev::Timer timer);
        Requests(CURLM *multi, ev::Timer timer, Options options);
        ~Requests();

    private:
        void onCURLTimer(long timeout);
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
            const URL url,
            const std::optional<Options> options,
            T payload
        ) {
            Options opt = options.value_or(mOptions);
            opt.headers["Content-Type"] = "application/json";

            auto connection = prepare(std::move(method), url, std::move(opt));
            CO_EXPECT(connection);

            curl_easy_setopt(
                connection->get()->easy.get(),
                CURLOPT_COPYPOSTFIELDS,
                nlohmann::json(payload).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str()
            );

            co_return co_await perform(std::move(*connection));
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
        ev::Timer mTimer;
        std::unique_ptr<CURLM, decltype(curl_multi_cleanup) *> mMulti;

        friend class Response;
    };

    tl::expected<std::shared_ptr<Requests>, std::error_code> makeRequests(const Options &options = {});
}

template<>
struct std::is_error_code_enum<asyncio::http::CURLError> : std::true_type {
};

template<>
struct std::is_error_code_enum<asyncio::http::CURLMError> : std::true_type {
};

#endif //ASYNCIO_REQUEST_H

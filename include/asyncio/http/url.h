#ifndef ASYNCIO_URL_H
#define ASYNCIO_URL_H

#include <memory>
#include <string>
#include <optional>
#include <system_error>
#include <tl/expected.hpp>
#include <curl/curl.h>
#include <zero/cmdline.h>
#include <zero/error.h>

namespace asyncio::http {
    class URL {
    public:
        DEFINE_ERROR_TRANSFORMER_TYPES(
            Error,
            "asyncio::http::url",
            [](const int v) { return curl_url_strerror(static_cast<CURLUcode>(v)); }
        )

        URL();
        explicit URL(CURLU *url);
        URL(const URL &rhs);
        URL(URL &&rhs) noexcept;

        URL &operator=(const URL &rhs);
        URL &operator=(URL &&rhs) noexcept;

        static tl::expected<URL, std::error_code> from(const std::string &str);

        [[nodiscard]] tl::expected<std::string, std::error_code> string() const;
        [[nodiscard]] tl::expected<std::string, std::error_code> scheme() const;
        [[nodiscard]] tl::expected<std::string, std::error_code> user() const;
        [[nodiscard]] tl::expected<std::string, std::error_code> password() const;
        [[nodiscard]] tl::expected<std::string, std::error_code> host() const;
        [[nodiscard]] tl::expected<std::string, std::error_code> path() const;
        [[nodiscard]] tl::expected<std::string, std::error_code> query() const;
        [[nodiscard]] tl::expected<unsigned short, std::error_code> port() const;

        URL &scheme(const std::optional<std::string> &scheme);
        URL &user(const std::optional<std::string> &user);
        URL &password(const std::optional<std::string> &password);
        URL &host(const std::optional<std::string> &host);
        URL &path(const std::optional<std::string> &path);
        URL &query(const std::optional<std::string> &query);
        URL &port(std::optional<unsigned short> port);

        URL &appendQuery(const std::string &query);
        URL &appendQuery(const std::string &key, const std::string &value);

        template<typename T>
            requires std::is_same_v<T, bool>
        URL &appendQuery(const std::string &key, T value) {
            return appendQuery(key, value ? "true" : "false");
        }

        template<typename T>
            requires std::is_arithmetic_v<T>
        URL &appendQuery(const std::string &key, T value) {
            return appendQuery(key, std::to_string(value));
        }

        URL &append(const std::string &subPath);

        template<typename T>
            requires std::is_arithmetic_v<T>
        URL &append(T subPath) {
            return append(std::to_string(subPath));
        }

    private:
        std::unique_ptr<CURLU, decltype(curl_url_cleanup) *> mURL;
    };

    DEFINE_MAKE_ERROR_CODE(URL::Error)
}

DECLARE_ERROR_CODE(asyncio::http::URL::Error)

template<>
tl::expected<asyncio::http::URL, std::error_code> zero::scan(std::string_view input);

#endif //ASYNCIO_URL_H

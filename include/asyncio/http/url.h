#ifndef ASYNCIO_URL_H
#define ASYNCIO_URL_H

#include <memory>
#include <string>
#include <optional>
#include <system_error>
#include <tl/expected.hpp>
#include <curl/curl.h>
#include <zero/cmdline.h>

namespace asyncio::http {
    enum URLError {

    };

    class URLCategory : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
    };

    const std::error_category &getURLCategory();
    std::error_code make_error_code(URLError e);

    class URL {
    public:
        URL();
        explicit URL(CURLU *url);
        URL(const URL &rhs);
        URL(URL &&rhs) noexcept;

    public:
        URL &operator=(const URL &rhs);
        URL &operator=(URL &&rhs) noexcept;

    public:
        static tl::expected<URL, std::error_code> from(const std::string &str);

    public:
        [[nodiscard]] tl::expected<std::string, std::error_code> string() const;

    public:
        [[nodiscard]] tl::expected<std::string, std::error_code> scheme() const;
        [[nodiscard]] tl::expected<std::string, std::error_code> user() const;
        [[nodiscard]] tl::expected<std::string, std::error_code> password() const;
        [[nodiscard]] tl::expected<std::string, std::error_code> host() const;
        [[nodiscard]] tl::expected<std::string, std::error_code> path() const;
        [[nodiscard]] tl::expected<std::string, std::error_code> query() const;
        [[nodiscard]] tl::expected<unsigned short, std::error_code> port() const;

    public:
        URL &scheme(const std::optional<std::string> &scheme);
        URL &user(const std::optional<std::string> &user);
        URL &password(const std::optional<std::string> &password);
        URL &host(const std::optional<std::string> &host);
        URL &path(const std::optional<std::string> &path);
        URL &query(const std::optional<std::string> &query);
        URL &port(std::optional<unsigned short> port);

    public:
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
}

template<>
tl::expected<asyncio::http::URL, std::error_code> zero::fromCommandLine(const std::string &str);

namespace std {
    template<>
    struct is_error_code_enum<asyncio::http::URLError> : public true_type {

    };
}

#endif //ASYNCIO_URL_H

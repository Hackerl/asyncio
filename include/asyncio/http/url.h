#ifndef ASYNCIO_URL_H
#define ASYNCIO_URL_H

#include <memory>
#include <curl/curl.h>
#include <zero/cmdline.h>
#include <zero/error.h>
#include <fmt/std.h>

namespace asyncio::http {
    std::string urlEscape(const std::string &str);
    std::expected<std::string, std::error_code> urlUnescape(const std::string &str);

    class URL {
    public:
        DEFINE_ERROR_TRANSFORMER_INNER(
            Error,
            "asyncio::http::url",
            [](const int value) { return curl_url_strerror(static_cast<CURLUcode>(value)); }
        )

        explicit URL(std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url);
        URL(const URL &rhs);
        URL(URL &&rhs) noexcept;

        URL &operator=(const URL &rhs);
        URL &operator=(URL &&rhs) noexcept;

        static std::expected<URL, std::error_code> from(const std::string &str);

        [[nodiscard]] std::string string() const;
        [[nodiscard]] std::string scheme() const;
        [[nodiscard]] std::optional<std::string> user() const;
        [[nodiscard]] std::optional<std::string> password() const;
        [[nodiscard]] std::optional<std::string> host() const;
        [[nodiscard]] std::string path() const;
        [[nodiscard]] std::string rawPath() const;
        [[nodiscard]] std::optional<std::string> query() const;
        [[nodiscard]] std::optional<std::string> rawQuery() const;
        [[nodiscard]] std::optional<std::string> fragment() const;
        [[nodiscard]] std::optional<std::uint16_t> port() const;

        URL &scheme(const std::string &scheme);
        URL &user(const std::optional<std::string> &user);
        URL &password(const std::optional<std::string> &password);
        URL &host(const std::optional<std::string> &host);
        URL &path(const std::string &path);
        URL &query(const std::optional<std::string> &query);
        URL &fragment(const std::optional<std::string> &fragment);
        URL &port(std::optional<std::uint16_t> port);

        URL &appendQuery(const std::string &query);
        URL &appendQuery(const std::string &key, const std::string &value);

        // `char *` will be implicitly converted to bool, not std::string.
        template<typename T>
            requires std::is_same_v<T, bool>
        URL &appendQuery(const std::string &key, const T value) {
            return appendQuery(key, value ? "true" : "false");
        }

        template<typename T>
            requires (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
        URL &appendQuery(const std::string &key, const T value) {
            return appendQuery(key, std::to_string(value));
        }

        URL &append(const std::string &subPath);

        template<typename T>
            requires std::is_arithmetic_v<T>
        URL &append(T subPath) {
            return append(std::to_string(subPath));
        }

    private:
        std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> mURL;
    };
}

DECLARE_ERROR_CODE(asyncio::http::URL::Error)

template<>
std::expected<asyncio::http::URL, std::error_code> zero::scan(std::string_view input);

template<typename Char>
struct fmt::formatter<asyncio::http::URL, Char> {
    template<typename ParseContext>
    static constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template<typename FmtContext>
    static auto format(const asyncio::http::URL &url, FmtContext &ctx) {
        return std::ranges::copy(fmt::to_string(url.string()), ctx.out()).out;
    }
};

#endif //ASYNCIO_URL_H

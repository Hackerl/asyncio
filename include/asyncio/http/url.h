#ifndef ASYNCIO_URL_H
#define ASYNCIO_URL_H

#include <memory>
#include <cassert>
#include <curl/curl.h>
#include <zero/cmdline.h>
#include <zero/error.h>
#include <fmt/std.h>

namespace asyncio::http {
    std::string urlEscape(const std::string &str);
    std::expected<std::string, std::error_code> urlUnescape(const std::string &str);

    class URL {
    public:
        Z_DEFINE_ERROR_TRANSFORMER_INNER(
            Error,
            "asyncio::http::url",
            [](const int value) { return curl_url_strerror(static_cast<CURLUcode>(value)); }
        )

    private:
        template<typename F>
            requires std::is_same_v<std::invoke_result_t<F>, CURLUcode>
        static std::expected<void, std::error_code> expected(F &&f) {
            if (const auto code = f(); code != CURLUE_OK)
                return std::unexpected{make_error_code(static_cast<Error>(code))};

            return {};
        }

    public:
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

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&scheme(this Self &&self, const std::string &scheme) {
            if (const auto result = expected([&] {
                return curl_url_set(self.mURL.get(), CURLUPART_SCHEME, scheme.c_str(), 0);
            }); !result)
                throw std::system_error{result.error()};

            return std::forward<Self>(self);
        }

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&user(this Self &&self, const std::optional<std::string> &user) {
            if (const auto result = expected([&] {
                return curl_url_set(self.mURL.get(), CURLUPART_USER, user ? user->c_str() : nullptr, CURLU_URLENCODE);
            }); !result)
                throw std::system_error{result.error()};

            return std::forward<Self>(self);
        }

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&password(this Self &&self, const std::optional<std::string> &password) {
            if (const auto result = expected([&] {
                return curl_url_set(
                    self.mURL.get(),
                    CURLUPART_PASSWORD,
                    password ? password->c_str() : nullptr,
                    CURLU_URLENCODE
                );
            }); !result)
                throw std::system_error{result.error()};

            return std::forward<Self>(self);
        }

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&host(this Self &&self, const std::optional<std::string> &host) {
            if (const auto result = expected([&] {
                return curl_url_set(self.mURL.get(), CURLUPART_HOST, host ? host->c_str() : nullptr, 0);
            }); !result)
                throw std::system_error{result.error()};

            return std::forward<Self>(self);
        }

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&path(this Self &&self, const std::string &path) {
            if (const auto result = expected([&] {
                return curl_url_set(self.mURL.get(), CURLUPART_PATH, path.c_str(), CURLU_URLENCODE);
            }); !result)
                throw std::system_error{result.error()};

            return std::forward<Self>(self);
        }

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&query(this Self &&self, const std::optional<std::string> &query) {
            if (const auto result = expected([&] {
                return curl_url_set(self.mURL.get(), CURLUPART_QUERY, query ? query->c_str() : nullptr, 0);
            }); !result)
                throw std::system_error{result.error()};

            return std::forward<Self>(self);
        }

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&fragment(this Self &&self, const std::optional<std::string> &fragment) {
            if (const auto result = expected([&] {
                return curl_url_set(
                    self.mURL.get(),
                    CURLUPART_FRAGMENT,
                    fragment ? fragment->c_str() : nullptr,
                    CURLU_URLENCODE
                );
            }); !result)
                throw std::system_error{result.error()};

            return std::forward<Self>(self);
        }

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&port(this Self &&self, const std::optional<std::uint16_t> port) {
            if (const auto result = expected([&] {
                return curl_url_set(self.mURL.get(), CURLUPART_PORT, port ? std::to_string(*port).c_str() : nullptr, 0);
            }); !result)
                throw std::system_error{result.error()};

            return std::forward<Self>(self);
        }

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&appendQuery(this Self &&self, const std::string &query) {
            if (const auto result = expected([&] {
                return curl_url_set(
                    self.mURL.get(),
                    CURLUPART_QUERY,
                    query.c_str(),
                    CURLU_APPENDQUERY | CURLU_URLENCODE
                );
            }); !result)
                throw std::system_error{result.error()};

            return std::forward<Self>(self);
        }

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&appendQuery(this Self &&self, const std::string &key, const std::string &value) {
            return std::forward<Self>(self).appendQuery(key + "=" + value);
        }

        // `char *` will be implicitly converted to bool, not std::string.
        template<typename Self, typename T>
            requires std::is_same_v<T, bool>
        Self &&appendQuery(this Self &&self, const std::string &key, const T value) {
            return std::forward<Self>(self).appendQuery(key, value ? "true" : "false");
        }

        template<typename Self, typename T>
            requires (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
        Self &&appendQuery(this Self &&self, const std::string &key, const T value) {
            return std::forward<Self>(self).appendQuery(key, std::to_string(value));
        }

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&append(this Self &&self, const std::string &subPath) {
            assert(!subPath.empty());
            assert(subPath.front() != '/');

            if (const auto parent = self.path(); parent.back() != '/')
                self.path(parent + '/' + subPath);
            else
                self.path(parent + subPath);

            return std::forward<Self>(self);
        }

        template<typename Self, typename T>
            requires (!std::is_const_v<Self> && std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
        Self &&append(this Self &&self, T subPath) {
            return std::forward<Self>(self).append(std::to_string(subPath));
        }

    private:
        std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> mURL;
    };

    std::strong_ordering operator<=>(const URL &lhs, const URL &rhs);
    bool operator==(const URL &lhs, const URL &rhs);
}

Z_DECLARE_ERROR_CODE(asyncio::http::URL::Error)

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

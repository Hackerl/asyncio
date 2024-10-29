#include <asyncio/http/url.h>
#include <cassert>

template<typename F>
    requires std::is_same_v<std::invoke_result_t<F>, CURLUcode>
std::expected<void, std::error_code> expected(F &&f) {
    if (const auto code = f(); code != CURLUE_OK)
        return std::unexpected{static_cast<asyncio::http::URL::Error>(code)};

    return {};
}

std::string asyncio::http::urlEscape(const std::string &str) {
    const std::unique_ptr<char, decltype(&curl_free)> ptr{
        curl_easy_escape(nullptr, str.c_str(), static_cast<int>(str.length())),
        curl_free
    };

    if (!ptr)
        throw std::system_error{errno, std::generic_category()};

    return ptr.get();
}

std::expected<std::string, std::error_code> asyncio::http::urlUnescape(const std::string &str) {
    int length{};

    const std::unique_ptr<char, decltype(&curl_free)> ptr{
        curl_easy_unescape(nullptr, str.c_str(), static_cast<int>(str.length()), &length),
        curl_free
    };

    if (!ptr)
        return std::unexpected{std::error_code{errno, std::generic_category()}};

    return std::string{ptr.get(), static_cast<std::size_t>(length)};
}

asyncio::http::URL::URL(std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url) : mURL{std::move(url)} {
}

asyncio::http::URL::URL(const URL &rhs) : mURL{curl_url_dup(rhs.mURL.get()), curl_url_cleanup} {
    if (!mURL)
        throw std::system_error{errno, std::generic_category()};
}

asyncio::http::URL::URL(URL &&rhs) noexcept: mURL{std::move(rhs.mURL)} {
}

asyncio::http::URL &asyncio::http::URL::operator=(const URL &rhs) {
    mURL = {curl_url_dup(rhs.mURL.get()), curl_url_cleanup};

    if (!mURL)
        throw std::system_error{errno, std::generic_category()};

    return *this;
}

asyncio::http::URL &asyncio::http::URL::operator=(URL &&rhs) noexcept {
    mURL = std::move(rhs.mURL);
    return *this;
}

std::expected<asyncio::http::URL, std::error_code> asyncio::http::URL::from(const std::string &str) {
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url{curl_url(), curl_url_cleanup};

    if (!url)
        return std::unexpected{std::error_code{errno, std::generic_category()}};

    EXPECT(expected([&] {
        return curl_url_set(url.get(), CURLUPART_URL, str.c_str(), CURLU_NON_SUPPORT_SCHEME);
    }));

    return URL{std::move(url)};
}

std::string asyncio::http::URL::string() const {
    char *url{};

    if (const auto result = expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_URL, &url, 0);
    }); !result)
        throw std::system_error{result.error()};

    return std::unique_ptr<char, decltype(&curl_free)>{url, curl_free}.get();
}

std::string asyncio::http::URL::scheme() const {
    char *scheme{};

    if (const auto result = expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_SCHEME, &scheme, 0);
    }); !result)
        throw std::system_error{result.error()};

    return std::unique_ptr<char, decltype(&curl_free)>{scheme, curl_free}.get();
}

std::optional<std::string> asyncio::http::URL::user() const {
    char *user{};

    if (const auto result = expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_USER, &user, CURLU_URLDECODE);
    }); !result) {
        if (result.error() == static_cast<Error>(CURLUE_NO_USER))
            return std::nullopt;

        throw std::system_error{result.error()};
    }

    return std::unique_ptr<char, decltype(&curl_free)>{user, curl_free}.get();
}

std::optional<std::string> asyncio::http::URL::password() const {
    char *password{};

    if (const auto result = expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_PASSWORD, &password, CURLU_URLDECODE);
    }); !result) {
        if (result.error() == static_cast<Error>(CURLUE_NO_PASSWORD))
            return std::nullopt;

        throw std::system_error{result.error()};
    }

    return std::unique_ptr<char, decltype(&curl_free)>{password, curl_free}.get();
}

std::optional<std::string> asyncio::http::URL::host() const {
    char *host{};

    if (const auto result = expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_HOST, &host, 0);
    }); !result) {
        if (result.error() == static_cast<Error>(CURLUE_NO_HOST))
            return std::nullopt;

        throw std::system_error{result.error()};
    }

    return std::unique_ptr<char, decltype(&curl_free)>{host, curl_free}.get();
}

std::string asyncio::http::URL::path() const {
    char *path{};

    if (const auto result = expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_PATH, &path, CURLU_URLDECODE);
    }); !result)
        throw std::system_error{result.error()};

    return std::unique_ptr<char, decltype(&curl_free)>{path, curl_free}.get();
}

std::string asyncio::http::URL::rawPath() const {
    char *path{};

    if (const auto result = expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_PATH, &path, 0);
    }); !result)
        throw std::system_error{result.error()};

    return std::unique_ptr<char, decltype(&curl_free)>{path, curl_free}.get();
}

std::optional<std::string> asyncio::http::URL::query() const {
    char *query{};

    if (const auto result = expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_QUERY, &query, CURLU_URLDECODE);
    }); !result) {
        if (result.error() == static_cast<Error>(CURLUE_NO_QUERY))
            return std::nullopt;

        throw std::system_error{result.error()};
    }

    return std::unique_ptr<char, decltype(&curl_free)>{query, curl_free}.get();
}

std::optional<std::string> asyncio::http::URL::rawQuery() const {
    char *query{};

    if (const auto result = expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_QUERY, &query, 0);
    }); !result) {
        if (result.error() == static_cast<Error>(CURLUE_NO_QUERY))
            return std::nullopt;

        throw std::system_error{result.error()};
    }

    return std::unique_ptr<char, decltype(&curl_free)>{query, curl_free}.get();
}

std::optional<std::string> asyncio::http::URL::fragment() const {
    char *fragment{};

    if (const auto result = expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_FRAGMENT, &fragment, CURLU_URLDECODE);
    }); !result) {
        if (result.error() == static_cast<Error>(CURLUE_NO_FRAGMENT))
            return std::nullopt;

        throw std::system_error{result.error()};
    }

    return std::unique_ptr<char, decltype(&curl_free)>{fragment, curl_free}.get();
}

std::optional<std::uint16_t> asyncio::http::URL::port() const {
    char *port{};

    if (const auto result = expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_PORT, &port, CURLU_DEFAULT_PORT);
    }); !result) {
        if (result.error() == static_cast<Error>(CURLUE_NO_PORT))
            return std::nullopt;

        throw std::system_error{result.error()};
    }

    const auto number = zero::strings::toNumber<std::uint16_t>(
        std::unique_ptr<char, decltype(&curl_free)>{port, curl_free}.get()
    );

    if (!number)
        throw std::system_error{number.error()};

    if (*number == 0)
        return std::nullopt;

    return *number;
}

asyncio::http::URL &asyncio::http::URL::scheme(const std::string &scheme) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_SCHEME, scheme.c_str(), 0);
    }); !result)
        throw std::system_error{result.error()};

    return *this;
}

asyncio::http::URL &asyncio::http::URL::user(const std::optional<std::string> &user) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_USER, user ? user->c_str() : nullptr, CURLU_URLENCODE);
    }); !result)
        throw std::system_error{result.error()};

    return *this;
}

asyncio::http::URL &asyncio::http::URL::password(const std::optional<std::string> &password) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_PASSWORD, password ? password->c_str() : nullptr, CURLU_URLENCODE);
    }); !result)
        throw std::system_error{result.error()};

    return *this;
}

asyncio::http::URL &asyncio::http::URL::host(const std::optional<std::string> &host) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_HOST, host ? host->c_str() : nullptr, 0);
    }); !result)
        throw std::system_error{result.error()};

    return *this;
}

asyncio::http::URL &asyncio::http::URL::path(const std::string &path) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_PATH, path.c_str(), CURLU_URLENCODE);
    }); !result)
        throw std::system_error{result.error()};

    return *this;
}

asyncio::http::URL &asyncio::http::URL::query(const std::optional<std::string> &query) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_QUERY, query ? query->c_str() : nullptr, 0);
    }); !result)
        throw std::system_error{result.error()};

    return *this;
}

asyncio::http::URL &asyncio::http::URL::fragment(const std::optional<std::string> &fragment) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_FRAGMENT, fragment ? fragment->c_str() : nullptr, CURLU_URLENCODE);
    }); !result)
        throw std::system_error{result.error()};

    return *this;
}

asyncio::http::URL &asyncio::http::URL::port(const std::optional<std::uint16_t> port) {
    if (const auto result = expected([&] {
        return curl_url_set(
            mURL.get(),
            CURLUPART_PORT,
            port ? std::to_string(*port).c_str() : nullptr,
            0
        );
    }); !result)
        throw std::system_error{result.error()};

    return *this;
}

asyncio::http::URL &asyncio::http::URL::appendQuery(const std::string &query) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_QUERY, query.c_str(),CURLU_APPENDQUERY | CURLU_URLENCODE);
    }); !result)
        throw std::system_error{result.error()};

    return *this;
}

asyncio::http::URL &asyncio::http::URL::appendQuery(const std::string &key, const std::string &value) {
    return appendQuery(key + "=" + value);
}

asyncio::http::URL &asyncio::http::URL::append(const std::string &subPath) {
    assert(!subPath.empty());
    assert(subPath.front() != '/');

    if (const auto parent = path(); parent.back() != '/')
        path(parent + '/' + subPath);
    else
        path(parent + subPath);

    return *this;
}

template<>
std::expected<asyncio::http::URL, std::error_code> zero::scan(const std::string_view input) {
    return asyncio::http::URL::from({input.begin(), input.end()});
}

DEFINE_ERROR_CATEGORY_INSTANCE(asyncio::http::URL::Error)

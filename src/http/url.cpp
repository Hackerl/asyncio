#include <asyncio/http/url.h>

template<typename F>
    requires std::is_same_v<std::invoke_result_t<F>, CURLUcode>
std::expected<void, std::error_code> expected(F &&f) {
    if (const CURLUcode code = f(); code != CURLUE_OK)
        return std::unexpected(static_cast<asyncio::http::URL::Error>(code));

    return {};
}

asyncio::http::URL::URL() : mURL(curl_url(), curl_url_cleanup) {
    if (!mURL)
        throw std::system_error(errno, std::generic_category());
}

asyncio::http::URL::URL(std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url) : mURL(std::move(url)) {
}

asyncio::http::URL::URL(const URL &rhs) : mURL(curl_url_dup(rhs.mURL.get()), curl_url_cleanup) {
    if (!mURL)
        throw std::system_error(errno, std::generic_category());
}

asyncio::http::URL::URL(URL &&rhs) noexcept: mURL(std::move(rhs.mURL)) {
}

asyncio::http::URL &asyncio::http::URL::operator=(const URL &rhs) {
    if (this == &rhs)
        return *this;

    mURL = {curl_url_dup(rhs.mURL.get()), curl_url_cleanup};

    if (!mURL)
        throw std::system_error(errno, std::generic_category());

    return *this;
}

asyncio::http::URL &asyncio::http::URL::operator=(URL &&rhs) noexcept {
    mURL = std::move(rhs.mURL);
    return *this;
}

std::expected<asyncio::http::URL, std::error_code> asyncio::http::URL::from(const std::string &str) {
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);

    if (!url)
        return std::unexpected(std::error_code(errno, std::generic_category()));

    EXPECT(expected([&] {
        return curl_url_set(url.get(), CURLUPART_URL, str.c_str(),CURLU_NON_SUPPORT_SCHEME);
    }));

    return URL{std::move(url)};
}

std::expected<std::string, std::error_code> asyncio::http::URL::string() const {
    char *url;

    EXPECT(expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_URL, &url, 0);
    }));

    return std::unique_ptr<char, decltype(&curl_free)>(url, curl_free).get();
}

std::expected<std::string, std::error_code> asyncio::http::URL::scheme() const {
    char *scheme;

    EXPECT(expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_SCHEME, &scheme, 0);
    }));

    return std::unique_ptr<char, decltype(&curl_free)>(scheme, curl_free).get();
}

std::expected<std::string, std::error_code> asyncio::http::URL::user() const {
    char *user;

    EXPECT(expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_USER, &user, 0);
    }));

    return std::unique_ptr<char, decltype(&curl_free)>(user, curl_free).get();
}

std::expected<std::string, std::error_code> asyncio::http::URL::password() const {
    char *password;

    EXPECT(expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_PASSWORD, &password, 0);
    }));

    return std::unique_ptr<char, decltype(&curl_free)>(password, curl_free).get();
}

std::expected<std::string, std::error_code> asyncio::http::URL::host() const {
    char *host;

    EXPECT(expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_HOST, &host, 0);
    }));

    return std::unique_ptr<char, decltype(&curl_free)>(host, curl_free).get();
}

std::expected<std::string, std::error_code> asyncio::http::URL::path() const {
    char *path;

    EXPECT(expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_PATH, &path, 0);
    }));

    return std::unique_ptr<char, decltype(&curl_free)>(path, curl_free).get();
}

std::expected<std::string, std::error_code> asyncio::http::URL::query() const {
    char *query;

    EXPECT(expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_QUERY, &query, 0);
    }));

    return std::unique_ptr<char, decltype(&curl_free)>(query, curl_free).get();
}

std::expected<unsigned short, std::error_code> asyncio::http::URL::port() const {
    char *port;

    EXPECT(expected([&] {
        return curl_url_get(mURL.get(), CURLUPART_PORT, &port, CURLU_DEFAULT_PORT);
    }));

    const auto n = zero::strings::toNumber<unsigned short>(
        std::unique_ptr<char, decltype(&curl_free)>(port, curl_free).get()
    );
    EXPECT(n);

    return *n;
}

asyncio::http::URL &asyncio::http::URL::scheme(const std::optional<std::string> &scheme) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_SCHEME, scheme ? scheme->c_str() : nullptr, 0);
    }); !result)
        throw std::system_error(result.error());

    return *this;
}

asyncio::http::URL &asyncio::http::URL::user(const std::optional<std::string> &user) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_USER, user ? user->c_str() : nullptr, 0);
    }); !result)
        throw std::system_error(result.error());

    return *this;
}

asyncio::http::URL &asyncio::http::URL::password(const std::optional<std::string> &password) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_PASSWORD, password ? password->c_str() : nullptr, 0);
    }); !result)
        throw std::system_error(result.error());

    return *this;
}

asyncio::http::URL &asyncio::http::URL::host(const std::optional<std::string> &host) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_HOST, host ? host->c_str() : nullptr, 0);
    }); !result)
        throw std::system_error(result.error());

    return *this;
}

asyncio::http::URL &asyncio::http::URL::path(const std::optional<std::string> &path) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_PATH, path ? path->c_str() : nullptr, 0);
    }); !result)
        throw std::system_error(result.error());

    return *this;
}

asyncio::http::URL &asyncio::http::URL::query(const std::optional<std::string> &query) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_QUERY, query ? query->c_str() : nullptr, 0);
    }); !result)
        throw std::system_error(result.error());

    return *this;
}

asyncio::http::URL &asyncio::http::URL::port(const std::optional<unsigned short> port) {
    if (const auto result = expected([&] {
        return curl_url_set(
            mURL.get(),
            CURLUPART_PORT,
            port ? std::to_string(*port).c_str() : nullptr,
            0
        );
    }); !result)
        throw std::system_error(result.error());

    return *this;
}

asyncio::http::URL &asyncio::http::URL::appendQuery(const std::string &query) {
    if (const auto result = expected([&] {
        return curl_url_set(mURL.get(), CURLUPART_QUERY, query.c_str(),CURLU_APPENDQUERY);
    }); !result)
        throw std::system_error(result.error());

    return *this;
}

asyncio::http::URL &asyncio::http::URL::appendQuery(const std::string &key, const std::string &value) {
    return appendQuery(key + "=" + value);
}

asyncio::http::URL &asyncio::http::URL::append(const std::string &subPath) {
    if (const std::string parent = path().value_or("/"); parent.back() != '/') {
        path(parent + '/' + subPath);
    }
    else {
        path(parent + subPath);
    }

    return *this;
}

template<>
std::expected<asyncio::http::URL, std::error_code> zero::scan(const std::string_view input) {
    return asyncio::http::URL::from({input.begin(), input.end()});
}

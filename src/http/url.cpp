#include <asyncio/http/url.h>

const char *asyncio::http::URLCategory::name() const noexcept {
    return "asyncio::http::url";
}

std::string asyncio::http::URLCategory::message(int value) const {
    return curl_url_strerror((CURLUcode) value);
}

const std::error_category &asyncio::http::getURLCategory() {
    static URLCategory instance;
    return instance;
}

std::error_code asyncio::http::make_error_code(URLError e) {
    return {static_cast<int>(e), getURLCategory()};
}

asyncio::http::URL::URL() : mURL(curl_url(), curl_url_cleanup) {
    if (!mURL)
        throw std::bad_alloc();
}

asyncio::http::URL::URL(CURLU *url) : mURL(url, curl_url_cleanup) {

}

asyncio::http::URL::URL(const asyncio::http::URL &rhs) : mURL(curl_url_dup(rhs.mURL.get()), curl_url_cleanup) {
    if (!mURL)
        throw std::bad_alloc();
}

asyncio::http::URL::URL(asyncio::http::URL &&rhs) noexcept: mURL(std::move(rhs.mURL)) {

}

asyncio::http::URL &asyncio::http::URL::operator=(const asyncio::http::URL &rhs) {
    if (this == &rhs)
        return *this;

    mURL = {curl_url_dup(rhs.mURL.get()), curl_url_cleanup};

    if (!mURL)
        throw std::bad_alloc();

    return *this;
}

asyncio::http::URL &asyncio::http::URL::operator=(asyncio::http::URL &&rhs) noexcept {
    mURL = std::move(rhs.mURL);
    return *this;
}

tl::expected<asyncio::http::URL, std::error_code> asyncio::http::URL::from(const std::string &str) {
    CURLU *url = curl_url();

    if (!url)
        return tl::unexpected(make_error_code(std::errc::not_enough_memory));

    CURLUcode code = curl_url_set(url, CURLUPART_URL, str.c_str(), CURLU_NON_SUPPORT_SCHEME);

    if (code != CURLUE_OK) {
        curl_url_cleanup(url);
        return tl::unexpected((URLError) code);
    }

    return URL{url};
}

tl::expected<std::string, std::error_code> asyncio::http::URL::string() const {
    char *url;
    CURLUcode code = curl_url_get(mURL.get(), CURLUPART_URL, &url, 0);

    if (code != CURLUE_OK)
        return tl::unexpected((URLError) code);

    return std::unique_ptr<char, decltype(curl_free) *>(url, curl_free).get();
}

tl::expected<std::string, std::error_code> asyncio::http::URL::scheme() const {
    char *scheme;
    CURLUcode code = curl_url_get(mURL.get(), CURLUPART_SCHEME, &scheme, 0);

    if (code != CURLUE_OK)
        return tl::unexpected((URLError) code);

    return std::unique_ptr<char, decltype(curl_free) *>(scheme, curl_free).get();
}

tl::expected<std::string, std::error_code> asyncio::http::URL::user() const {
    char *user;
    CURLUcode code = curl_url_get(mURL.get(), CURLUPART_USER, &user, 0);

    if (code != CURLUE_OK)
        return tl::unexpected((URLError) code);

    return std::unique_ptr<char, decltype(curl_free) *>(user, curl_free).get();
}

tl::expected<std::string, std::error_code> asyncio::http::URL::password() const {
    char *password;
    CURLUcode code = curl_url_get(mURL.get(), CURLUPART_PASSWORD, &password, 0);

    if (code != CURLUE_OK)
        return tl::unexpected((URLError) code);

    return std::unique_ptr<char, decltype(curl_free) *>(password, curl_free).get();
}

tl::expected<std::string, std::error_code> asyncio::http::URL::host() const {
    char *host;
    CURLUcode code = curl_url_get(mURL.get(), CURLUPART_HOST, &host, 0);

    if (code != CURLUE_OK)
        return tl::unexpected((URLError) code);

    return std::unique_ptr<char, decltype(curl_free) *>(host, curl_free).get();
}

tl::expected<std::string, std::error_code> asyncio::http::URL::path() const {
    char *path;
    CURLUcode code = curl_url_get(mURL.get(), CURLUPART_PATH, &path, 0);

    if (code != CURLUE_OK)
        return tl::unexpected((URLError) code);

    return std::unique_ptr<char, decltype(curl_free) *>(path, curl_free).get();
}

tl::expected<std::string, std::error_code> asyncio::http::URL::query() const {
    char *query;
    CURLUcode code = curl_url_get(mURL.get(), CURLUPART_QUERY, &query, 0);

    if (code != CURLUE_OK)
        return tl::unexpected((URLError) code);

    return std::unique_ptr<char, decltype(curl_free) *>(query, curl_free).get();
}

tl::expected<unsigned short, std::error_code> asyncio::http::URL::port() const {
    char *port;
    CURLUcode code = curl_url_get(mURL.get(), CURLUPART_PORT, &port, CURLU_DEFAULT_PORT);

    if (code != CURLUE_OK)
        return tl::unexpected((URLError) code);

    auto n = zero::strings::toNumber<unsigned short>(
            std::unique_ptr<char, decltype(curl_free) *>(port, curl_free).get()
    );

    if (!n)
        return tl::unexpected(make_error_code(std::errc::invalid_argument));

    return *n;
}

asyncio::http::URL &asyncio::http::URL::scheme(const std::optional<std::string> &scheme) {
    CURLUcode code = curl_url_set(mURL.get(), CURLUPART_SCHEME, scheme ? scheme->c_str() : nullptr, 0);

    if (code != CURLUE_OK)
        throw std::system_error((URLError) code);

    return *this;
}

asyncio::http::URL &asyncio::http::URL::user(const std::optional<std::string> &user) {
    CURLUcode code = curl_url_set(mURL.get(), CURLUPART_USER, user ? user->c_str() : nullptr, 0);

    if (code != CURLUE_OK)
        throw std::system_error((URLError) code);

    return *this;
}

asyncio::http::URL &asyncio::http::URL::password(const std::optional<std::string> &password) {
    CURLUcode code = curl_url_set(mURL.get(), CURLUPART_PASSWORD, password ? password->c_str() : nullptr, 0);

    if (code != CURLUE_OK)
        throw std::system_error((URLError) code);

    return *this;
}

asyncio::http::URL &asyncio::http::URL::host(const std::optional<std::string> &host) {
    CURLUcode code = curl_url_set(mURL.get(), CURLUPART_HOST, host ? host->c_str() : nullptr, 0);

    if (code != CURLUE_OK)
        throw std::system_error((URLError) code);

    return *this;
}

asyncio::http::URL &asyncio::http::URL::path(const std::optional<std::string> &path) {
    CURLUcode code = curl_url_set(mURL.get(), CURLUPART_PATH, path ? path->c_str() : nullptr, 0);

    if (code != CURLUE_OK)
        throw std::system_error((URLError) code);

    return *this;
}

asyncio::http::URL &asyncio::http::URL::query(const std::optional<std::string> &query) {
    CURLUcode code = curl_url_set(mURL.get(), CURLUPART_QUERY, query ? query->c_str() : nullptr, 0);

    if (code != CURLUE_OK)
        throw std::system_error((URLError) code);

    return *this;
}

asyncio::http::URL &asyncio::http::URL::port(std::optional<unsigned short> port) {
    CURLUcode code = curl_url_set(mURL.get(), CURLUPART_PORT, port ? std::to_string(*port).c_str() : nullptr, 0);

    if (code != CURLUE_OK)
        throw std::system_error((URLError) code);

    return *this;
}

asyncio::http::URL &asyncio::http::URL::appendQuery(const std::string &query) {
    CURLUcode code = curl_url_set(mURL.get(), CURLUPART_QUERY, query.c_str(), CURLU_APPENDQUERY);

    if (code != CURLUE_OK)
        throw std::system_error((URLError) code);

    return *this;
}

asyncio::http::URL &asyncio::http::URL::appendQuery(const std::string &key, const std::string &value) {
    return appendQuery(key + "=" + value);
}

asyncio::http::URL &asyncio::http::URL::append(const std::string &subPath) {
    std::string parent = path().value_or("/");

    if (parent.back() != '/') {
        path(parent + '/' + subPath);
    } else {
        path(parent + subPath);
    }

    return *this;
}

template<>
tl::expected<asyncio::http::URL, std::error_code> zero::scan(std::string_view input) {
    return asyncio::http::URL::from({input.begin(), input.end()});
}

#include <asyncio/http/request.h>
#include <asyncio/event_loop.h>
#include <asyncio/fs/file.h>
#include <asyncio/ev/event.h>
#include <asyncio/error.h>
#include <fcntl.h>
#include <mutex>

#ifdef ASYNCIO_EMBED_CA_CERT
#include <asyncio/net/ssl.h>
#endif

using namespace std::chrono_literals;

constexpr auto DEFAULT_CONNECT_TIMEOUT = 30s;
constexpr auto DEFAULT_TRANSFER_TIMEOUT = 1h;

std::size_t onWrite(const char *buffer, const std::size_t size, const std::size_t n, void *userdata) {
    const auto connection = static_cast<asyncio::http::Connection *>(userdata);

    if (connection->status == asyncio::http::Connection::NOT_STARTED) {
        connection->status = asyncio::http::Connection::TRANSFERRING;
        connection->promise.resolve();
    }
    else if (connection->status == asyncio::http::Connection::PAUSED) {
        connection->status = asyncio::http::Connection::TRANSFERRING;
        return size * n;
    }

    if (auto task = connection->buffers[0].writeAll(std::as_bytes(std::span{buffer, size * n})); !task.done()) {
        connection->status = asyncio::http::Connection::PAUSED;
        task.promise().then([=] {
            curl_easy_pause(connection->easy.get(), CURLPAUSE_CONT);
        });
        return CURL_WRITEFUNC_PAUSE;
    }

    return size * n;
}

const char *asyncio::http::CURLErrorCategory::name() const noexcept {
    return "asyncio::http::curl::easy";
}

std::string asyncio::http::CURLErrorCategory::message(int value) const {
    return curl_easy_strerror(static_cast<CURLcode>(value));
}

const std::error_category &asyncio::http::curlErrorCategory() {
    static CURLErrorCategory instance;
    return instance;
}

std::error_code asyncio::http::make_error_code(const CURLError e) {
    return {static_cast<int>(e), curlErrorCategory()};
}

const char *asyncio::http::CURLMErrorCategory::name() const noexcept {
    return "asyncio::http::curl::multi";
}

std::string asyncio::http::CURLMErrorCategory::message(int value) const {
    return curl_multi_strerror(static_cast<CURLMcode>(value));
}

const std::error_category &asyncio::http::curlMErrorCategory() {
    static CURLMErrorCategory instance;
    return instance;
}

std::error_code asyncio::http::make_error_code(const CURLMError e) {
    return {static_cast<int>(e), curlMErrorCategory()};
}

asyncio::http::Response::Response(std::shared_ptr<Requests> requests, std::unique_ptr<Connection> connection)
    : mRequests(std::move(requests)), mConnection(std::move(connection)) {
}

asyncio::http::Response::~Response() {
    if (!mConnection)
        return;

    curl_multi_remove_handle(mRequests->mMulti.get(), mConnection->easy.get());
}

long asyncio::http::Response::statusCode() const {
    long status = 0;
    curl_easy_getinfo(mConnection->easy.get(), CURLINFO_RESPONSE_CODE, &status);

    return status;
}

std::optional<curl_off_t> asyncio::http::Response::contentLength() const {
    curl_off_t length;

    if (curl_easy_getinfo(mConnection->easy.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length) != CURLE_OK ||
        length < 0)
        return std::nullopt;

    return length;
}

std::optional<std::string> asyncio::http::Response::contentType() const {
    const char *type = nullptr;

    if (curl_easy_getinfo(mConnection->easy.get(), CURLINFO_CONTENT_TYPE, &type) != CURLE_OK || !type)
        return std::nullopt;

    return type;
}

std::vector<std::string> asyncio::http::Response::cookies() const {
    curl_slist *list = nullptr;
    curl_easy_getinfo(mConnection->easy.get(), CURLINFO_COOKIELIST, &list);

    std::vector<std::string> cookies;

    for (curl_slist *ptr = list; ptr; ptr = ptr->next)
        cookies.emplace_back(ptr->data);

    curl_slist_free_all(list);

    return cookies;
}

std::optional<std::string> asyncio::http::Response::header(const std::string &name) const {
    curl_header *header = nullptr;

    if (curl_easy_header(mConnection->easy.get(), name.c_str(), 0, CURLH_HEADER, -1, &header) != CURLHE_OK || !header)
        return std::nullopt;

    return header->value;
}

zero::async::coroutine::Task<std::string, std::error_code> asyncio::http::Response::string() const {
    auto data = CO_TRY(co_await mConnection->buffers[1].readAll());

    if (mConnection->error)
        co_return tl::unexpected(*mConnection->error);

    co_return std::string{reinterpret_cast<const char *>(data->data()), data->size()};
}

zero::async::coroutine::Task<void, std::error_code> asyncio::http::Response::output(std::filesystem::path path) const {
    auto file = CO_TRY(fs::open(path, O_WRONLY | O_CREAT | O_TRUNC));
    CO_TRY(co_await copy(mConnection->buffers[1], *file));
    CO_TRY(co_await file->close());

    if (mConnection->error)
        co_return tl::unexpected(*mConnection->error);

    co_return tl::expected<void, std::error_code>{};
}

zero::async::coroutine::Task<nlohmann::json, std::error_code> asyncio::http::Response::json() const {
    auto content = CO_TRY(co_await string());

    try {
        co_return nlohmann::json::parse(std::move(*content));
    }
    catch (const nlohmann::json::exception &) {
        co_return tl::unexpected(make_error_code(std::errc::bad_message));
    }
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::http::Response::read(const std::span<std::byte> data) {
    return mConnection->buffers[1].read(data).transformError([this](const auto &ec) {
        return mConnection->error.value_or(ec);
    });
}

std::size_t asyncio::http::Response::capacity() {
    return mConnection->buffers[1].capacity();
}

std::size_t asyncio::http::Response::available() {
    return mConnection->buffers[1].available();
}

zero::async::coroutine::Task<std::string, std::error_code> asyncio::http::Response::readLine() {
    return mConnection->buffers[1].readLine().transformError([this](const auto &ec) {
        return mConnection->error.value_or(ec);
    });
}

zero::async::coroutine::Task<std::vector<std::byte>, std::error_code>
asyncio::http::Response::readUntil(const std::byte byte) {
    return mConnection->buffers[1].readUntil(byte).transformError([this](const auto &ec) {
        return mConnection->error.value_or(ec);
    });
}

zero::async::coroutine::Task<void, std::error_code> asyncio::http::Response::peek(const std::span<std::byte> data) {
    return mConnection->buffers[1].peek(data).transformError([this](const auto &ec) {
        return mConnection->error.value_or(ec);
    });
}

asyncio::http::Requests::Requests(CURLM *multi, ev::Timer timer) : Requests(multi, std::move(timer), {}) {
}

asyncio::http::Requests::Requests(CURLM *multi, ev::Timer timer, Options options)
    : mRunning(0), mOptions(std::move(options)), mTimer(std::move(timer)), mMulti(multi, curl_multi_cleanup) {
    curl_multi_setopt(
        mMulti.get(),
        CURLMOPT_SOCKETFUNCTION,
        static_cast<int (*)(CURL *, curl_socket_t, int, void *, void *)>(
            [](CURL *, const curl_socket_t s, const int what, void *userdata, void *data) {
                static_cast<Requests *>(userdata)->onCURLEvent(s, what, data);
                return 0;
            }
        )
    );

    curl_multi_setopt(mMulti.get(), CURLMOPT_SOCKETDATA, this);

    curl_multi_setopt(
        mMulti.get(),
        CURLMOPT_TIMERFUNCTION,
        static_cast<int (*)(CURLM *, long, void *)>([](CURLM *, const long timeout, void *userdata) {
            static_cast<Requests *>(userdata)->onCURLTimer(timeout);
            return 0;
        })
    );

    curl_multi_setopt(mMulti.get(), CURLMOPT_TIMERDATA, this);
}

asyncio::http::Requests::~Requests() {
    assert(mRunning == 0);
}

void asyncio::http::Requests::onCURLTimer(const long timeout) {
    if (timeout == -1) {
        mTimer.cancel();
        return;
    }

    if (mTimer.pending())
        mTimer.cancel();

    mTimer.after(std::chrono::milliseconds{timeout}).promise().then([this] {
        curl_multi_socket_action(mMulti.get(), CURL_SOCKET_TIMEOUT, 0, &mRunning);
        recycle();
    });
}

void asyncio::http::Requests::onCURLEvent(const curl_socket_t s, const int what, void *data) {
    struct Context {
        event e;
        std::shared_ptr<Requests> requests;
    };

    auto context = static_cast<Context *>(data);

    if (what == CURL_POLL_REMOVE) {
        if (!context)
            return;

        event_del(&context->e);
        delete context;
        return;
    }

    if (!context) {
        context = new Context{.requests = shared_from_this()};
        curl_multi_assign(mMulti.get(), s, context);
    }
    else {
        event_del(&context->e);
    }

    event_assign(
        &context->e,
        getEventLoop()->base(),
        static_cast<evutil_socket_t>(s),
        static_cast<short>((what & CURL_POLL_IN ? ev::READ : 0) | (what & CURL_POLL_OUT ? ev::WRITE : 0) | EV_PERSIST),
        [](const evutil_socket_t fd, const short w, void *arg) {
            const auto ctx = static_cast<Context *>(arg);
            const auto requests = ctx->requests;

            curl_multi_socket_action(
                requests->mMulti.get(),
                fd,
                (w & ev::READ ? CURL_CSELECT_IN : 0) | (w & ev::WRITE ? CURL_CSELECT_OUT : 0),
                &requests->mRunning
            );

            requests->recycle();

            if (requests->mRunning == 0 && requests->mTimer.pending())
                requests->mTimer.cancel();
        },
        context
    );

    event_add(&context->e, nullptr);
}

void asyncio::http::Requests::recycle() const {
    int n = 0;

    while (CURLMsg *msg = curl_multi_info_read(mMulti.get(), &n)) {
        if (msg->msg != CURLMSG_DONE)
            continue;

        Connection *connection;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &connection);

        connection->buffers[0].close();

        if (msg->data.result != CURLE_OK) {
            connection->error = static_cast<CURLError>(msg->data.result);

            if (connection->status != Connection::NOT_STARTED)
                continue;

            connection->promise.reject(static_cast<CURLError>(msg->data.result));
            continue;
        }

        if (connection->status != Connection::NOT_STARTED)
            continue;

        connection->promise.resolve();
    }
}

asyncio::http::Options &asyncio::http::Requests::options() {
    return mOptions;
}

tl::expected<std::unique_ptr<asyncio::http::Connection>, std::error_code>
asyncio::http::Requests::prepare(std::string method, const URL &url, const std::optional<Options> &options) {
    const auto u = TRY(url.string());
    std::unique_ptr<CURL, decltype(curl_easy_cleanup) *> easy = {curl_easy_init(), curl_easy_cleanup};

    if (!easy)
        return tl::unexpected(make_error_code(std::errc::not_enough_memory));

    auto buffers = TRY(ev::pipe());
    const auto [proxy, headers, cookies, timeout, connectTimeout, userAgent] = options.value_or(mOptions);

    zero::async::promise::Promise<void, std::error_code> promise;

    auto connection = std::make_unique<Connection>(
        std::move(*buffers),
        std::move(easy),
        promise
    );

    method = zero::strings::toupper(method);

    if (method == "HEAD")
        curl_easy_setopt(connection->easy.get(), CURLOPT_NOBODY, 1L);
    else if (method == "GET")
        curl_easy_setopt(connection->easy.get(), CURLOPT_HTTPGET, 1L);
    else if (method == "POST")
        curl_easy_setopt(connection->easy.get(), CURLOPT_POST, 1L);
    else
        curl_easy_setopt(connection->easy.get(), CURLOPT_CUSTOMREQUEST, method.c_str());

    curl_easy_setopt(connection->easy.get(), CURLOPT_URL, u->c_str());
    curl_easy_setopt(connection->easy.get(), CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(connection->easy.get(), CURLOPT_WRITEFUNCTION, onWrite);
    curl_easy_setopt(connection->easy.get(), CURLOPT_WRITEDATA, connection.get());
    curl_easy_setopt(connection->easy.get(), CURLOPT_PRIVATE, connection.get());
    curl_easy_setopt(connection->easy.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(connection->easy.get(), CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
    curl_easy_setopt(connection->easy.get(), CURLOPT_USERAGENT, userAgent.value_or("asyncio requests").c_str());
    curl_easy_setopt(connection->easy.get(), CURLOPT_ACCEPT_ENCODING, "");

    curl_easy_setopt(
        connection->easy.get(),
        CURLOPT_CONNECTTIMEOUT,
        static_cast<long>(connectTimeout.value_or(DEFAULT_CONNECT_TIMEOUT).count())
    );

    curl_easy_setopt(
        connection->easy.get(),
        CURLOPT_TIMEOUT,
        static_cast<long>(timeout.value_or(DEFAULT_TRANSFER_TIMEOUT).count())
    );

#ifdef ASYNCIO_EMBED_CA_CERT
    curl_easy_setopt(connection->easy.get(), CURLOPT_CAINFO, nullptr);
    curl_easy_setopt(connection->easy.get(), CURLOPT_CAPATH, nullptr);

    curl_easy_setopt(
        connection->easy.get(),
        CURLOPT_SSL_CTX_FUNCTION,
        static_cast<CURLcode (*)(CURL *, void *, void *)>(
            [](CURL *, void *ctx, void *) {
                if (!net::ssl::loadEmbeddedCA(static_cast<net::ssl::Context *>(ctx)))
                    return CURLE_ABORTED_BY_CALLBACK;

                return CURLE_OK;
            }
        )
    );
#endif

    if (proxy)
        curl_easy_setopt(connection->easy.get(), CURLOPT_PROXY, proxy->c_str());

    if (!cookies.empty()) {
        curl_easy_setopt(
            connection->easy.get(),
            CURLOPT_COOKIE,
            to_string(fmt::join(
                cookies | std::views::transform([](const auto it) { return it.first + "=" + it.second; }),
                "; "
            )).c_str()
        );
    }

    curl_slist *list = nullptr;

    for (const auto &[k, v]: headers)
        list = curl_slist_append(list, fmt::format("{}: {}", k, v).c_str());

    if (list) {
        curl_easy_setopt(connection->easy.get(), CURLOPT_HTTPHEADER, list);

        connection->defers.emplace_back([list] {
            curl_slist_free_all(list);
        });
    }

    return connection;
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::perform(std::unique_ptr<Connection> connection) {
    if (const CURLMcode code = curl_multi_add_handle(mMulti.get(), connection->easy.get()); code != CURLM_OK)
        co_return tl::unexpected(static_cast<CURLMError>(code));

    if (const auto result = co_await zero::async::coroutine::Cancellable{
        connection->promise,
        [connection = connection.get(), multi = mMulti.get()]() -> tl::expected<void, std::error_code> {
            if (const CURLMcode code = curl_multi_remove_handle(multi, connection->easy.get()); code != CURLM_OK)
                return tl::unexpected(static_cast<CURLMError>(code));

            connection->promise.reject(make_error_code(std::errc::operation_canceled));
            return {};
        }
    }; !result) {
        curl_multi_remove_handle(mMulti.get(), connection->easy.get());
        co_return tl::unexpected(result.error());
    }

    co_return Response{shared_from_this(), std::move(connection)};
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(std::string method, const URL url, const std::optional<Options> options) {
    auto connection = CO_TRY(prepare(std::move(method), url, options));
    co_return std::move(co_await perform(std::move(*connection)));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(
    std::string method,
    const URL url,
    const std::optional<Options> options,
    const std::string payload
) {
    auto connection = CO_TRY(prepare(std::move(method), url, options));

    curl_easy_setopt(connection->get()->easy.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.length()));
    curl_easy_setopt(connection->get()->easy.get(), CURLOPT_COPYPOSTFIELDS, payload.c_str());

    co_return std::move(co_await perform(std::move(*connection)));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(
    std::string method,
    const URL url,
    const std::optional<Options> options,
    std::map<std::string, std::string> payload
) {
    auto connection = CO_TRY(prepare(std::move(method), url, options));

    curl_easy_setopt(
        connection->get()->easy.get(),
        CURLOPT_COPYPOSTFIELDS,
        to_string(fmt::join(
            payload | std::views::transform([](const auto it) { return it.first + "=" + it.second; }),
            "&"
        )).c_str()
    );

    co_return std::move(co_await perform(std::move(*connection)));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(
    std::string method,
    const URL url,
    const std::optional<Options> options,
    std::map<std::string, std::filesystem::path> payload
) {
    auto connection = CO_TRY(prepare(std::move(method), url, options));
    curl_mime *form = curl_mime_init(connection->get()->easy.get());

    for (const auto &[key, value]: payload) {
        curl_mimepart *field = curl_mime_addpart(form);
        curl_mime_name(field, key.c_str());

        if (const CURLcode code = curl_mime_filedata(field, value.string().c_str()); code != CURLE_OK) {
            curl_mime_free(form);
            co_return tl::unexpected(static_cast<CURLError>(code));
        }
    }

    curl_easy_setopt(connection->get()->easy.get(), CURLOPT_MIMEPOST, form);

    connection->get()->defers.emplace_back([form] {
        curl_mime_free(form);
    });

    co_return std::move(co_await perform(std::move(*connection)));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(
    std::string method,
    const URL url,
    const std::optional<Options> options,
    std::map<std::string, std::variant<std::string, std::filesystem::path>> payload
) {
    auto connection = CO_TRY(prepare(std::move(method), url, options));
    curl_mime *form = curl_mime_init(connection->get()->easy.get());

    for (const auto &[k, v]: payload) {
        curl_mimepart *field = curl_mime_addpart(form);
        curl_mime_name(field, k.c_str());

        if (v.index() == 0) {
            curl_mime_data(field, std::get<std::string>(v).c_str(), CURL_ZERO_TERMINATED);
            continue;
        }

        if (const CURLcode code = curl_mime_filedata(
            field,
            std::get<std::filesystem::path>(v).string().c_str()
        ); code != CURLE_OK) {
            curl_mime_free(form);
            co_return tl::unexpected(static_cast<CURLError>(code));
        }
    }

    curl_easy_setopt(connection->get()->easy.get(), CURLOPT_MIMEPOST, form);

    connection->get()->defers.emplace_back([form] {
        curl_mime_free(form);
    });

    co_return std::move(co_await perform(std::move(*connection)));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(
    std::string method,
    const URL url,
    const std::optional<Options> options,
    const nlohmann::json payload
) {
    Options opt = options.value_or(mOptions);
    opt.headers["Content-Type"] = "application/json";

    auto connection = CO_TRY(prepare(std::move(method), url, opt));

    curl_easy_setopt(
        connection->get()->easy.get(),
        CURLOPT_COPYPOSTFIELDS,
        payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str()
    );

    co_return std::move(co_await perform(std::move(*connection)));
}

tl::expected<std::shared_ptr<asyncio::http::Requests>, std::error_code>
asyncio::http::makeRequests(const Options &options) {
    static std::once_flag flag;

    std::call_once(flag, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        std::atexit([] {
            curl_global_cleanup();
        });
    });

    CURLM *multi = curl_multi_init();

    if (!multi)
        return tl::unexpected(make_error_code(std::errc::not_enough_memory));

    auto timer = ev::makeTimer();

    if (!timer) {
        curl_multi_cleanup(multi);
        return tl::unexpected(timer.error());
    }

    return std::make_shared<Requests>(multi, std::move(*timer), options);
}

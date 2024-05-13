#include <asyncio/http/request.h>
#include <asyncio/event_loop.h>
#include <asyncio/fs/file.h>
#include <asyncio/ev/event.h>
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

    if (connection->status == asyncio::http::Connection::Status::NOT_STARTED) {
        connection->status = asyncio::http::Connection::Status::TRANSFERRING;
        connection->promise.resolve();
    }
    else if (connection->status == asyncio::http::Connection::Status::PAUSED) {
        connection->status = asyncio::http::Connection::Status::TRANSFERRING;
        return size * n;
    }

    if (auto task = connection->buffers[0].writeAll(std::as_bytes(std::span{buffer, size * n})); !task.done()) {
        connection->status = asyncio::http::Connection::Status::PAUSED;
        task.future().then([=] {
            curl_easy_pause(connection->easy.get(), CURLPAUSE_CONT);
        });
        return CURL_WRITEFUNC_PAUSE;
    }

    return size * n;
}

asyncio::http::Response::Response(Requests *requests, std::unique_ptr<Connection> connection)
    : mRequests(requests), mConnection(std::move(connection)) {
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
    const auto data = co_await mConnection->buffers[1].readAll();
    CO_EXPECT(data);

    if (mConnection->error)
        co_return tl::unexpected(*mConnection->error);

    co_return std::string{reinterpret_cast<const char *>(data->data()), data->size()};
}

zero::async::coroutine::Task<void, std::error_code>
asyncio::http::Response::output(const std::filesystem::path path) const {
    auto file = fs::open(path, O_WRONLY | O_CREAT | O_TRUNC);
    CO_EXPECT(file);

    CO_EXPECT(co_await copy(mConnection->buffers[1], *file));
    CO_EXPECT(co_await file->close());

    if (mConnection->error)
        co_return tl::unexpected(*mConnection->error);

    co_return {};
}

zero::async::coroutine::Task<nlohmann::json, std::error_code> asyncio::http::Response::json() const {
    auto content = co_await string();
    CO_EXPECT(content);

    try {
        co_return nlohmann::json::parse(*std::move(content));
    }
    catch (const nlohmann::json::exception &) {
        co_return tl::unexpected(Error::INVALID_JSON);
    }
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::http::Response::read(const std::span<std::byte> data) {
    return mConnection->buffers[1].read(data).transformError([this](const auto &ec) {
        return mConnection->error.value_or(ec);
    });
}

std::size_t asyncio::http::Response::capacity() const {
    return mConnection->buffers[1].capacity();
}

std::size_t asyncio::http::Response::available() const {
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

asyncio::http::Requests::Requests(CURLM *multi, std::unique_ptr<event, decltype(event_free) *> timer)
    : Requests(multi, std::move(timer), {}) {
}

asyncio::http::Requests::Requests(CURLM *multi, std::unique_ptr<event, decltype(event_free) *> timer, Options options)
    : mRunning(0), mOptions(std::move(options)), mTimer(std::move(timer)), mMulti(multi, curl_multi_cleanup) {
    evtimer_assign(
        mTimer.get(),
        event_get_base(mTimer.get()),
        [](evutil_socket_t, short, void *arg) {
            static_cast<Requests *>(arg)->onTimer();
        },
        this
    );

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

asyncio::http::Requests::Requests(Requests &&rhs) noexcept
    : mRunning(rhs.mRunning), mOptions(std::move(rhs.mOptions)),
      mTimer(std::move(rhs.mTimer)), mMulti(std::move(rhs.mMulti)) {
    assert(mRunning == 0);
    const auto timer = mTimer.get();
    evtimer_assign(timer, event_get_base(timer), event_get_callback(timer), this);

    curl_multi_setopt(mMulti.get(), CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(mMulti.get(), CURLMOPT_TIMERDATA, this);
}

asyncio::http::Requests &asyncio::http::Requests::operator=(Requests &&rhs) noexcept {
    assert(rhs.mRunning == 0);

    mRunning = rhs.mRunning;
    mOptions = std::move(rhs.mOptions);
    mTimer = std::move(rhs.mTimer);
    mMulti = std::move(rhs.mMulti);

    const auto timer = mTimer.get();
    evtimer_assign(timer, event_get_base(timer), event_get_callback(timer), this);

    curl_multi_setopt(mMulti.get(), CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(mMulti.get(), CURLMOPT_TIMERDATA, this);

    return *this;
}

asyncio::http::Requests::~Requests() {
    assert(mRunning == 0);
}

tl::expected<asyncio::http::Requests, std::error_code> asyncio::http::Requests::make(const Options &options) {
    static std::once_flag flag;

    std::call_once(flag, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        std::atexit([] {
            curl_global_cleanup();
        });
    });

    CURLM *multi = curl_multi_init();

    if (!multi)
        return tl::unexpected<std::error_code>(errno, std::generic_category());

    auto timer = std::unique_ptr<event, decltype(event_free) *>(
        evtimer_new(getEventLoop()->base(), nullptr, nullptr),
        event_free
    );

    if (!timer) {
        curl_multi_cleanup(multi);
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());
    }

    return Requests{multi, std::move(timer), options};
}

void asyncio::http::Requests::onTimer() {
    curl_multi_socket_action(mMulti.get(), CURL_SOCKET_TIMEOUT, 0, &mRunning);
    recycle();
}

void asyncio::http::Requests::onCURLTimer(const long timeout) const {
    if (timeout == -1) {
        evtimer_del(mTimer.get());
        return;
    }

    const timeval tv = {
        static_cast<decltype(timeval::tv_sec)>(timeout / 1000),
        static_cast<decltype(timeval::tv_usec)>(timeout % 1000 * 1000)
    };

    evtimer_add(mTimer.get(), &tv);
}

void asyncio::http::Requests::onCURLEvent(const curl_socket_t s, const int what, void *data) {
    struct Context {
        event e;
        Requests *requests;
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
        context = new Context{.requests = this};
        curl_multi_assign(mMulti.get(), s, context);
    }
    else {
        event_del(&context->e);
    }

    event_assign(
        &context->e,
        getEventLoop()->base(),
#ifdef _WIN32
        static_cast<evutil_socket_t>(s),
#else
        s,
#endif
        static_cast<short>((what & CURL_POLL_IN ? ev::What::READ : 0) | (what & CURL_POLL_OUT ? ev::What::WRITE : 0) | EV_PERSIST),
        [](const evutil_socket_t fd, const short w, void *arg) {
            const auto ctx = static_cast<Context *>(arg);
            const auto requests = ctx->requests;

            curl_multi_socket_action(
                requests->mMulti.get(),
                fd,
                (w & ev::What::READ ? CURL_CSELECT_IN : 0) | (w & ev::What::WRITE ? CURL_CSELECT_OUT : 0),
                &requests->mRunning
            );

            requests->recycle();

            if (requests->mRunning == 0 && evtimer_pending(requests->mTimer.get(), nullptr))
                evtimer_del(requests->mTimer.get());
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

            if (connection->status != Connection::Status::NOT_STARTED)
                continue;

            connection->promise.reject(static_cast<CURLError>(msg->data.result));
            continue;
        }

        if (connection->status != Connection::Status::NOT_STARTED)
            continue;

        connection->promise.resolve();
    }
}

asyncio::http::Options &asyncio::http::Requests::options() {
    return mOptions;
}

tl::expected<std::unique_ptr<asyncio::http::Connection>, std::error_code>
asyncio::http::Requests::prepare(std::string method, const URL &url, const std::optional<Options> &options) {
    const auto u = url.string();
    EXPECT(u);

    std::unique_ptr<CURL, decltype(curl_easy_cleanup) *> easy = {curl_easy_init(), curl_easy_cleanup};

    if (!easy)
        return tl::unexpected<std::error_code>(errno, std::generic_category());

    auto buffers = ev::pipe();
    EXPECT(buffers);

    const auto [proxy, headers, cookies, timeout, connectTimeout, userAgent] = options.value_or(mOptions);

    auto connection = std::make_unique<Connection>(*std::move(buffers), std::move(easy));
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
        connection->promise.getFuture(),
        [connection = connection.get(), multi = mMulti.get()]() -> tl::expected<void, std::error_code> {
            if (connection->promise.isFulfilled())
                return tl::unexpected(zero::async::coroutine::Error::WILL_BE_DONE);

            if (const CURLMcode code = curl_multi_remove_handle(multi, connection->easy.get()); code != CURLM_OK)
                return tl::unexpected(static_cast<CURLMError>(code));

            connection->promise.reject(zero::async::coroutine::Error::CANCELLED);
            return {};
        }
    }; !result) {
        curl_multi_remove_handle(mMulti.get(), connection->easy.get());
        co_return tl::unexpected(result.error());
    }

    co_return Response{this, std::move(connection)};
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(std::string method, const URL url, const std::optional<Options> options) {
    auto connection = prepare(std::move(method), url, options);
    CO_EXPECT(connection);
    co_return co_await perform(*std::move(connection));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(
    std::string method,
    const URL url,
    const std::optional<Options> options,
    const std::string payload
) {
    auto connection = prepare(std::move(method), url, options);
    CO_EXPECT(connection);

    curl_easy_setopt(connection->get()->easy.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.length()));
    curl_easy_setopt(connection->get()->easy.get(), CURLOPT_COPYPOSTFIELDS, payload.c_str());

    co_return co_await perform(*std::move(connection));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(
    std::string method,
    const URL url,
    const std::optional<Options> options,
    std::map<std::string, std::string> payload
) {
    auto connection = prepare(std::move(method), url, options);
    CO_EXPECT(connection);

    curl_easy_setopt(
        connection->get()->easy.get(),
        CURLOPT_COPYPOSTFIELDS,
        to_string(fmt::join(
            payload | std::views::transform([](const auto it) { return it.first + "=" + it.second; }),
            "&"
        )).c_str()
    );

    co_return co_await perform(*std::move(connection));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(
    std::string method,
    const URL url,
    const std::optional<Options> options,
    std::map<std::string, std::filesystem::path> payload
) {
    auto connection = prepare(std::move(method), url, options);
    CO_EXPECT(connection);

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

    co_return co_await perform(*std::move(connection));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(
    std::string method,
    const URL url,
    const std::optional<Options> options,
    std::map<std::string, std::variant<std::string, std::filesystem::path>> payload
) {
    auto connection = prepare(std::move(method), url, options);
    CO_EXPECT(connection);

    curl_mime *form = curl_mime_init(connection->get()->easy.get());

    for (const auto &[k, v]: payload) {
        curl_mimepart *field = curl_mime_addpart(form);
        curl_mime_name(field, k.c_str());

        if (std::holds_alternative<std::string>(v)) {
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

    co_return co_await perform(*std::move(connection));
}

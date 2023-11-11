#include <asyncio/http/request.h>
#include <asyncio/thread.h>
#include <asyncio/event_loop.h>
#include <fstream>

#ifdef ASYNCIO_EMBED_CA_CERT
#include <asyncio/net/ssl.h>
#endif

using namespace std::chrono_literals;

size_t onWrite(char *buffer, size_t size, size_t n, void *userdata) {
    auto connection = static_cast<asyncio::http::Connection *>(userdata);

    if (!connection->transferring) {
        connection->transferring = true;
        connection->promise.resolve();
    }

    if (connection->buffers[0].pending() >= 1024 * 1024) {
        connection->buffers[0].drain().promise().then([=]() {
            curl_easy_pause(connection->easy.get(), CURLPAUSE_CONT);
        });

        return CURL_WRITEFUNC_PAUSE;
    }

    if (!connection->buffers[0].submit({(const std::byte *) buffer, size * n}))
        return CURL_WRITEFUNC_ERROR;

    return size * n;
}

const char *asyncio::http::CURLCategory::name() const noexcept {
    return "asyncio::http::curl::easy";
}

std::string asyncio::http::CURLCategory::message(int value) const {
    return curl_easy_strerror((CURLcode) value);
}

const std::error_category &asyncio::http::getCURLCategory() {
    static CURLCategory instance;
    return instance;
}

std::error_code asyncio::http::make_error_code(CURLError e) {
    return {static_cast<int>(e), getCURLCategory()};
}

const char *asyncio::http::CURLMCategory::name() const noexcept {
    return "asyncio::http::curl::multi";
}

std::string asyncio::http::CURLMCategory::message(int value) const {
    return curl_multi_strerror((CURLMcode) value);
}

const std::error_category &asyncio::http::getCURLMCategory() {
    static CURLMCategory instance;
    return instance;
}

std::error_code asyncio::http::make_error_code(CURLMError e) {
    return {static_cast<int>(e), getCURLMCategory()};
}

asyncio::http::Response::Response(std::shared_ptr<Requests> requests, std::unique_ptr<Connection> connection)
        : mRequests(std::move(requests)), mConnection(std::move(connection)) {

}

asyncio::http::Response::~Response() {
    if (!mConnection)
        return;

    curl_multi_remove_handle(mRequests->mMulti.get(), mConnection->easy.get());
}

long asyncio::http::Response::statusCode() {
    long status = 0;
    curl_easy_getinfo(mConnection->easy.get(), CURLINFO_RESPONSE_CODE, &status);

    return status;
}

std::optional<curl_off_t> asyncio::http::Response::contentLength() {
    curl_off_t length;

    if (curl_easy_getinfo(mConnection->easy.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length) != CURLE_OK ||
        length < 0)
        return std::nullopt;

    return length;
}

std::optional<std::string> asyncio::http::Response::contentType() {
    char *type = nullptr;

    if (curl_easy_getinfo(mConnection->easy.get(), CURLINFO_CONTENT_TYPE, &type) != CURLE_OK || !type)
        return std::nullopt;

    return type;
}

std::vector<std::string> asyncio::http::Response::cookies() {
    curl_slist *list = nullptr;
    curl_easy_getinfo(mConnection->easy.get(), CURLINFO_COOKIELIST, &list);

    std::vector<std::string> cookies;

    for (curl_slist *ptr = list; ptr; ptr = ptr->next)
        cookies.emplace_back(ptr->data);

    curl_slist_free_all(list);

    return cookies;
}

std::optional<std::string> asyncio::http::Response::header(const std::string &name) {
    curl_header *header = nullptr;

    if (curl_easy_header(mConnection->easy.get(), name.c_str(), 0, CURLH_HEADER, -1, &header) != CURLHE_OK || !header)
        return std::nullopt;

    return header->value;
}

zero::async::coroutine::Task<std::string, std::error_code> asyncio::http::Response::string() {
    auto data = CO_TRY(co_await readAll(std::move(mConnection->buffers[1])));
    co_return std::string{(const char *) data->data(), data->size()};
}

zero::async::coroutine::Task<void, std::error_code> asyncio::http::Response::output(std::filesystem::path path) {
    auto stream = std::make_shared<std::ofstream>(path, std::ios::binary);

    if (!stream->is_open())
        co_return tl::unexpected(std::error_code(errno, std::system_category()));

    tl::expected<void, std::error_code> result;

    while (true) {
        std::byte data[10240];
        auto n = co_await operator*().read(data);

        if (!n) {
            if (n.error() == Error::IO_EOF)
                break;

            result = tl::unexpected(n.error());
            break;
        }

        auto res = co_await asyncio::toThread(
                [=, data = std::vector<std::byte>{data, data + *n}]() -> tl::expected<void, std::error_code> {
                    stream->write((const char *) data.data(), (std::streamsize) data.size());

                    if (!stream->good())
                        return tl::unexpected(std::error_code(errno, std::system_category()));

                    return {};
                }
        );

        if (!res) {
            result = tl::unexpected(res.error());
            break;
        }
    }

    stream->close();
    co_return result;
}

asyncio::ev::IBufferReader &asyncio::http::Response::operator*() {
    return mConnection->buffers[1];
}

asyncio::ev::IBufferReader *asyncio::http::Response::operator->() {
    return &mConnection->buffers[1];
}

asyncio::http::Requests::Requests(CURLM *multi, ev::Timer timer)
        : Requests(multi, std::move(timer), {}) {

}

asyncio::http::Requests::Requests(CURLM *multi, ev::Timer timer, Options options)
        : mMulti(multi, curl_multi_cleanup), mTimer(std::move(timer)), mOptions(std::move(options)), mRunning(0) {
    curl_multi_setopt(
            mMulti.get(),
            CURLMOPT_SOCKETFUNCTION,
            static_cast<int (*)(CURL *, curl_socket_t, int, void *, void *)>(
                    [](CURL *easy, curl_socket_t s, int what,
                       void *userdata, void *data) {
                        static_cast<Requests *>(userdata)->onCURLEvent(easy, s, what, data);
                        return 0;
                    }
            )
    );

    curl_multi_setopt(mMulti.get(), CURLMOPT_SOCKETDATA, this);

    curl_multi_setopt(
            mMulti.get(),
            CURLMOPT_TIMERFUNCTION,
            static_cast<int (*)(CURLM *, long, void *)>([](CURLM *multi, long timeout, void *userdata) {
                static_cast<Requests *>(userdata)->onCURLTimer(timeout);
                return 0;
            })
    );

    curl_multi_setopt(mMulti.get(), CURLMOPT_TIMERDATA, this);
}

asyncio::http::Requests::~Requests() {
    assert(mRunning == 0);
}

void asyncio::http::Requests::onCURLTimer(long timeout) {
    if (timeout == -1) {
        mTimer.cancel();
        return;
    }

    if (mTimer.pending())
        mTimer.cancel();

    mTimer.after(std::chrono::milliseconds{timeout}).promise().then([this]() {
        curl_multi_socket_action(mMulti.get(), CURL_SOCKET_TIMEOUT, 0, &mRunning);
        recycle();
    });
}

void asyncio::http::Requests::onCURLEvent(CURL *easy, curl_socket_t s, int what, void *data) {
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
    } else {
        event_del(&context->e);
    }

    event_assign(
            &context->e,
            getEventLoop()->base(),
            s,
            (short) ((((what & CURL_POLL_IN) ? ev::READ : 0) | ((what & CURL_POLL_OUT) ? ev::WRITE : 0)) | EV_PERSIST),
            [](evutil_socket_t fd, short what, void *arg) {
                auto context = static_cast<Context *>(arg);
                auto requests = context->requests;

                curl_multi_socket_action(
                        requests->mMulti.get(),
                        fd,
                        ((what & ev::READ) ? CURL_CSELECT_IN : 0) | ((what & ev::WRITE) ? CURL_CSELECT_OUT : 0),
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

void asyncio::http::Requests::recycle() {
    int n = 0;

    while (CURLMsg *msg = curl_multi_info_read(mMulti.get(), &n)) {
        if (msg->msg != CURLMSG_DONE)
            continue;

        Connection *connection;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &connection);

        if (msg->data.result != CURLE_OK) {
            if (!connection->transferring)
                connection->promise.reject((CURLError) msg->data.result);
            else
                connection->buffers[0].throws((CURLError) msg->data.result);

            continue;
        }

        if (!connection->transferring)
            connection->promise.resolve();

        connection->buffers[0].close();
    }
}

asyncio::http::Options &asyncio::http::Requests::options() {
    return mOptions;
}

tl::expected<std::unique_ptr<asyncio::http::Connection>, std::error_code>
asyncio::http::Requests::prepare(std::string method, const URL &url, const std::optional<Options> &options) {
    auto u = TRY(url.string());
    std::unique_ptr<CURL, decltype(curl_easy_cleanup) *> easy = {curl_easy_init(), curl_easy_cleanup};

    if (!easy)
        return tl::unexpected(make_error_code(std::errc::not_enough_memory));

    auto buffers = TRY(ev::pipe());
    Options opt = options.value_or(mOptions);
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
    curl_easy_setopt(connection->easy.get(), CURLOPT_CONNECTTIMEOUT, (long) opt.timeout.value_or(30s).count());
    curl_easy_setopt(connection->easy.get(), CURLOPT_USERAGENT, opt.userAgent.value_or("asyncio requests").c_str());
    curl_easy_setopt(connection->easy.get(), CURLOPT_ACCEPT_ENCODING, "");

#ifdef ASYNCIO_EMBED_CA_CERT
    curl_easy_setopt(connection->easy.get(), CURLOPT_CAINFO, nullptr);
    curl_easy_setopt(connection->easy.get(), CURLOPT_CAPATH, nullptr);

    curl_easy_setopt(
            connection->easy.get(),
            CURLOPT_SSL_CTX_FUNCTION,
            static_cast<CURLcode (*)(CURL *, void *, void *)>(
                    [](CURL *curl, void *ctx, void *parm) {
                        if (!net::ssl::loadEmbeddedCA((net::ssl::Context *) ctx))
                            return CURLE_ABORTED_BY_CALLBACK;

                        return CURLE_OK;
                    }
            )
    );
#endif

    if (opt.proxy)
        curl_easy_setopt(connection->easy.get(), CURLOPT_PROXY, opt.proxy->c_str());

    if (!opt.cookies.empty()) {
        curl_easy_setopt(
                connection->easy.get(),
                CURLOPT_COOKIE,
                fmt::to_string(fmt::join(
                        opt.cookies | std::views::transform([](const auto it) { return it.first + "=" + it.second; }),
                        "; "
                )).c_str()
        );
    }

    curl_slist *headers = nullptr;

    for (const auto &[k, v]: opt.headers)
        headers = curl_slist_append(headers, fmt::format("{}: {}", k, v).c_str());

    if (headers) {
        curl_easy_setopt(connection->easy.get(), CURLOPT_HTTPHEADER, headers);

        connection->defers.emplace_back([headers]() {
            curl_slist_free_all(headers);
        });
    }

    return connection;
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::perform(std::unique_ptr<Connection> connection) {
    CURLMcode code = curl_multi_add_handle(mMulti.get(), connection->easy.get());

    if (code != CURLM_OK)
        co_return tl::unexpected((CURLMError) code);

    auto result = co_await zero::async::coroutine::Cancellable{
            connection->promise,
            [connection = connection.get(), multi = mMulti.get()]() -> tl::expected<void, std::error_code> {
                CURLMcode code = curl_multi_remove_handle(multi, connection->easy.get());

                if (code != CURLM_OK)
                    return tl::unexpected((CURLMError) code);

                connection->promise.reject(make_error_code(std::errc::operation_canceled));
                return {};
            }
    };

    if (!result) {
        curl_multi_remove_handle(mMulti.get(), connection->easy.get());
        co_return tl::unexpected(result.error());
    }

    co_return Response{shared_from_this(), std::move(connection)};
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(std::string method, asyncio::http::URL url, std::optional<Options> options) {
    auto connection = CO_TRY(prepare(std::move(method), url, options));
    co_return std::move(co_await perform(std::move(*connection)));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code> asyncio::http::Requests::request(
        std::string method,
        asyncio::http::URL url,
        std::optional<Options> options,
        std::string payload
) {
    auto connection = CO_TRY(prepare(std::move(method), url, options));

    curl_easy_setopt(connection->get()->easy.get(), CURLOPT_POSTFIELDSIZE, (long) payload.length());
    curl_easy_setopt(connection->get()->easy.get(), CURLOPT_COPYPOSTFIELDS, payload.c_str());

    co_return std::move(co_await perform(std::move(*connection)));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code> asyncio::http::Requests::request(
        std::string method,
        asyncio::http::URL url,
        std::optional<Options> options,
        std::map<std::string, std::string> payload
) {
    auto connection = CO_TRY(prepare(std::move(method), url, options));

    curl_easy_setopt(
            connection->get()->easy.get(),
            CURLOPT_COPYPOSTFIELDS,
            fmt::to_string(fmt::join(
                    payload | std::views::transform([](const auto it) { return it.first + "=" + it.second; }),
                    "&"
            )).c_str()
    );

    co_return std::move(co_await perform(std::move(*connection)));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code> asyncio::http::Requests::request(
        std::string method,
        asyncio::http::URL url,
        std::optional<Options> options,
        std::map<std::string, std::filesystem::path> payload
) {
    auto connection = CO_TRY(prepare(std::move(method), url, options));
    curl_mime *form = curl_mime_init(connection->get()->easy.get());

    for (const auto &[key, value]: payload) {
        curl_mimepart *field = curl_mime_addpart(form);
        curl_mime_name(field, key.c_str());

        CURLcode c = curl_mime_filedata(field, value.string().c_str());

        if (c != CURLE_OK) {
            curl_mime_free(form);
            co_return tl::unexpected((CURLError) c);
        }
    }

    curl_easy_setopt(connection->get()->easy.get(), CURLOPT_MIMEPOST, form);

    connection->get()->defers.emplace_back([form]() {
        curl_mime_free(form);
    });

    co_return std::move(co_await perform(std::move(*connection)));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code> asyncio::http::Requests::request(
        std::string method,
        asyncio::http::URL url,
        std::optional<Options> options,
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

        CURLcode c = curl_mime_filedata(field, std::get<std::filesystem::path>(v).string().c_str());

        if (c != CURLE_OK) {
            curl_mime_free(form);
            co_return tl::unexpected((CURLError) c);
        }
    }

    curl_easy_setopt(connection->get()->easy.get(), CURLOPT_MIMEPOST, form);

    connection->get()->defers.emplace_back([form]() {
        curl_mime_free(form);
    });

    co_return std::move(co_await perform(std::move(*connection)));
}

zero::async::coroutine::Task<asyncio::http::Response, std::error_code> asyncio::http::Requests::request(
        std::string method,
        asyncio::http::URL url,
        std::optional<Options> options,
        nlohmann::json payload
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
asyncio::http::makeRequests(const asyncio::http::Options &options) {
    static std::once_flag flag;

    std::call_once(flag, []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        std::atexit([]() {
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

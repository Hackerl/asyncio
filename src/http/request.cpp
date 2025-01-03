#include <asyncio/http/request.h>
#include <asyncio/fs.h>
#include <zero/defer.h>

using namespace std::chrono_literals;

constexpr auto DEFAULT_CONNECT_TIMEOUT = 30s;
constexpr auto DEFAULT_TRANSFER_TIMEOUT = 1h;

template<typename F>
    requires std::is_same_v<std::invoke_result_t<F>, CURLcode>
std::expected<void, std::error_code> expected(F &&f) {
    if (const auto code = f(); code != CURLE_OK)
        return std::unexpected{static_cast<asyncio::http::Requests::CURLError>(code)};

    return {};
}

template<typename F>
    requires std::is_same_v<std::invoke_result_t<F>, CURLMcode>
std::expected<void, std::error_code> expected(F &&f) {
    if (const auto code = f(); code != CURLM_OK)
        return std::unexpected{static_cast<asyncio::http::Requests::CURLMError>(code)};

    return {};
}

std::size_t onWrite(const char *buffer, const std::size_t size, const std::size_t n, void *userdata) {
    const auto total = size * n;
    const auto connection = static_cast<asyncio::http::Connection *>(userdata);

    if (!connection->transferring) {
        connection->transferring = true;
        connection->promise.resolve();
    }

    if (total == 0)
        return 0;

    if (!connection->dataPromise)
        return CURL_WRITEFUNC_PAUSE;

    const auto need = total - connection->skip;

    if (connection->buffer.size() < need) {
        std::memcpy(
            connection->buffer.data(),
            reinterpret_cast<const std::byte *>(buffer) + connection->skip,
            connection->buffer.size()
        );

        std::exchange(connection->dataPromise, std::nullopt)->resolve(connection->buffer.size());
        connection->skip += connection->buffer.size();
        return CURL_WRITEFUNC_PAUSE;
    }

    std::memcpy(connection->buffer.data(), reinterpret_cast<const std::byte *>(buffer) + connection->skip, need);
    std::exchange(connection->dataPromise, std::nullopt)->resolve(need);

    connection->skip = 0;
    return total;
}

asyncio::http::Response::Response(Requests *requests, std::unique_ptr<Connection> connection)
    : mRequests{requests}, mConnection{std::move(connection)} {
}

asyncio::http::Response::~Response() {
    if (!mConnection)
        return;

    curl_multi_remove_handle(mRequests->mCore->multi.get(), mConnection->easy.get());
}

long asyncio::http::Response::statusCode() const {
    long status{};
    curl_easy_getinfo(mConnection->easy.get(), CURLINFO_RESPONSE_CODE, &status);

    return status;
}

std::optional<std::uint64_t> asyncio::http::Response::contentLength() const {
    curl_off_t length{};

    if (!expected([&] {
        return curl_easy_getinfo(mConnection->easy.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
    }) || length < 0)
        return std::nullopt;

    return length;
}

std::optional<std::string> asyncio::http::Response::contentType() const {
    const char *type{};

    if (!expected([&] {
        return curl_easy_getinfo(mConnection->easy.get(), CURLINFO_CONTENT_TYPE, &type);
    }) || !type)
        return std::nullopt;

    return type;
}

std::vector<std::string> asyncio::http::Response::cookies() const {
    curl_slist *list{};
    curl_easy_getinfo(mConnection->easy.get(), CURLINFO_COOKIELIST, &list);

    std::vector<std::string> cookies;

    for (const auto *ptr = list; ptr; ptr = ptr->next)
        cookies.emplace_back(ptr->data);

    // passing a null pointer is ok
    curl_slist_free_all(list);
    return cookies;
}

std::optional<std::string> asyncio::http::Response::header(const std::string &name) const {
    curl_header *header{};

    if (curl_easy_header(mConnection->easy.get(), name.c_str(), 0, CURLH_HEADER, -1, &header) != CURLHE_OK || !header)
        return std::nullopt;

    return header->value;
}

asyncio::task::Task<std::string, std::error_code> asyncio::http::Response::string() {
    StringWriter writer;
    CO_EXPECT(co_await copy(*this, writer));
    co_return *std::move(writer);
}

// ReSharper disable once CppMemberFunctionMayBeConst
asyncio::task::Task<void, std::error_code>
asyncio::http::Response::output(std::filesystem::path path) {
    auto file = co_await fs::open(std::move(path), O_WRONLY | O_CREAT | O_TRUNC);
    CO_EXPECT(file);
    CO_EXPECT(co_await copy(*this, *file));
    co_return {};
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::http::Response::read(const std::span<std::byte> data) {
    if (mConnection->finished) {
        if (mConnection->error)
            co_return std::unexpected{*mConnection->error};

        co_return 0;
    }

    mConnection->buffer = data;
    mConnection->dataPromise.emplace();

    auto future = mConnection->dataPromise->getFuture();

    CO_EXPECT(expected([&] {
        return curl_easy_pause(mConnection->easy.get(), CURLPAUSE_RECV_CONT);
    }));

    co_return co_await task::Cancellable{
        std::move(future),
        [this]() -> std::expected<void, std::error_code> {
            if (!mConnection->dataPromise)
                return std::unexpected{task::Error::WILL_BE_DONE};

            EXPECT(expected([&] {
                return curl_easy_pause(mConnection->easy.get(), CURLPAUSE_RECV);
            }));

            std::exchange(mConnection->dataPromise, std::nullopt)->reject(task::Error::CANCELLED);
            return {};
        }
    };
}

// ReSharper disable once CppMemberFunctionMayBeConst
void asyncio::http::Requests::Core::recycle() {
    int n{};

    while (const auto msg = curl_multi_info_read(multi.get(), &n)) {
        if (msg->msg != CURLMSG_DONE)
            continue;

        Connection *connection{};
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &connection);

        connection->finished = true;

        if (msg->data.result != CURLE_OK) {
            connection->error = static_cast<CURLError>(msg->data.result);

            if (!connection->transferring) {
                connection->promise.reject(*connection->error);
                continue;
            }

            if (!connection->dataPromise)
                continue;

            std::exchange(connection->dataPromise, std::nullopt)->reject(*connection->error);
            continue;
        }

        if (!connection->transferring) {
            connection->promise.resolve();
            continue;
        }

        if (!connection->dataPromise)
            continue;

        std::exchange(connection->dataPromise, std::nullopt)->resolve(0);
    }
}

std::expected<void, std::error_code> asyncio::http::Requests::Core::setTimer(const long ms) {
    if (ms == -1) {
        uv_timer_stop(timer.raw());
        return {};
    }

    EXPECT(uv::expected([&] {
        return uv_timer_start(
            timer.raw(),
            [](auto *handle) {
                uv_timer_stop(handle);
                auto &core = *static_cast<Core *>(handle->data);
                curl_multi_socket_action(core.multi.get(), CURL_SOCKET_TIMEOUT, 0, &core.running);
                core.recycle();
            },
            ms,
            0
        );
    }));

    return {};
}

std::expected<void, std::error_code>
asyncio::http::Requests::Core::handle(const curl_socket_t s, const int action, Context *context) {
    if (action == CURL_POLL_REMOVE) {
        if (!context)
            return {};

        uv_poll_stop(context->poll.raw());
        delete context;
        curl_multi_assign(multi.get(), s, nullptr);
        return {};
    }

    if (!context) {
        auto poll = std::make_unique<uv_poll_t>();

        EXPECT(uv::expected([&] {
            return uv_poll_init_socket(getEventLoop()->raw(), poll.get(), s);
        }));

        context = new Context{uv::Handle{std::move(poll)}, this, s};
        context->poll->data = context;
        curl_multi_assign(multi.get(), s, context);
    }

    EXPECT(uv::expected([&] {
        return uv_poll_start(
            context->poll.raw(),
            (action & CURL_POLL_IN ? UV_READABLE : 0) | (action & CURL_POLL_OUT ? UV_WRITABLE : 0),
            [](auto *handle, const int status, const int e) {
                const auto ctx = static_cast<const Context *>(handle->data);
                const auto core = ctx->core;

                /*
                 * This function may execute the CURL_POLL_REMOVE action,
                 * causing the Context to be deleted and no longer accessible,
                 * so save the core pointer in advance.
                 */
                curl_multi_socket_action(
                    core->multi.get(),
                    ctx->s,
                    status < 0
                        ? CURL_CSELECT_ERR
                        : (e & UV_READABLE ? CURL_CSELECT_IN : 0) | (e & UV_WRITABLE ? CURL_CSELECT_OUT : 0),
                    &core->running
                );

                core->recycle();
            }
        );
    }));

    return {};
}

asyncio::http::Requests::Requests(std::unique_ptr<Core> core) : mCore{std::move(core)} {
}

std::expected<asyncio::http::Requests, std::error_code> asyncio::http::Requests::make(Options options) {
    static std::once_flag flag;

    std::call_once(flag, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        std::atexit([] {
            curl_global_cleanup();
        });
    });

    std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> multi{curl_multi_init(), curl_multi_cleanup};

    if (!multi)
        return std::unexpected{std::error_code{errno, std::generic_category()}};

    auto timer = std::make_unique<uv_timer_t>();

    EXPECT(uv::expected([&] {
        return uv_timer_init(getEventLoop()->raw(), timer.get());
    }));

    auto core = std::make_unique<Core>(
        0,
        std::move(options),
        uv::Handle(std::move(timer)),
        std::move(multi)
    );

    core->timer->data = core.get();

    curl_multi_setopt(
        core->multi.get(),
        CURLMOPT_SOCKETFUNCTION,
        static_cast<curl_socket_callback>(
            [](CURL *, const curl_socket_t s, const int action, void *ctx, void *socketContext) {
                if (!static_cast<Core *>(ctx)->handle(s, action, static_cast<Core::Context *>(socketContext)))
                    return -1;

                return 0;
            }
        )
    );

    curl_multi_setopt(core->multi.get(), CURLMOPT_SOCKETDATA, core.get());

    curl_multi_setopt(
        core->multi.get(),
        CURLMOPT_TIMERFUNCTION,
        static_cast<curl_multi_timer_callback>([](CURLM *, const long ms, void *ctx) {
            if (!static_cast<Core *>(ctx)->setTimer(ms))
                return -1;

            return 0;
        })
    );

    curl_multi_setopt(core->multi.get(), CURLMOPT_TIMERDATA, core.get());

    return Requests{std::move(core)};
}

asyncio::http::Options &asyncio::http::Requests::options() {
    return mCore->options;
}

const asyncio::http::Options &asyncio::http::Requests::options() const {
    return mCore->options;
}

// ReSharper disable once CppMemberFunctionMayBeConst
std::expected<std::unique_ptr<asyncio::http::Connection>, std::error_code>
asyncio::http::Requests::prepare(std::string method, const URL &url, const std::optional<Options> &options) {
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy{curl_easy_init(), curl_easy_cleanup};

    if (!easy)
        return std::unexpected{std::error_code{errno, std::generic_category()}};

    const auto [
        proxy,
        headers,
        cookies,
        timeout,
        connectTimeout,
        userAgent,
        tls,
        hooks
    ] = options.value_or(mCore->options);

    auto connection = std::make_unique<Connection>(std::move(easy));

    method = zero::strings::toupper(method);

    if (method == "HEAD")
        curl_easy_setopt(connection->easy.get(), CURLOPT_NOBODY, 1L);
    else if (method == "GET")
        curl_easy_setopt(connection->easy.get(), CURLOPT_HTTPGET, 1L);
    else if (method == "POST")
        curl_easy_setopt(connection->easy.get(), CURLOPT_POST, 1L);
    else
        curl_easy_setopt(connection->easy.get(), CURLOPT_CUSTOMREQUEST, method.c_str());

    curl_easy_setopt(connection->easy.get(), CURLOPT_URL, url.string().c_str());
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

    if (proxy)
        curl_easy_setopt(connection->easy.get(), CURLOPT_PROXY, proxy->c_str());

    if (!cookies.empty()) {
        curl_easy_setopt(
            connection->easy.get(),
            CURLOPT_COOKIE,
            to_string(fmt::join(
                cookies | std::views::transform([](const auto &it) { return it.first + "=" + it.second; }),
                "; "
            )).c_str()
        );
    }

    curl_slist *list{nullptr};

    for (const auto &[k, v]: headers)
        list = curl_slist_append(list, fmt::format("{}: {}", k, v).c_str());

    if (list) {
        curl_easy_setopt(connection->easy.get(), CURLOPT_HTTPHEADER, list);

        connection->defers.emplace_back([list] {
            curl_slist_free_all(list);
        });
    }

    const auto &[insecure, ca, cert, privateKey, password] = tls;

    curl_easy_setopt(connection->easy.get(), CURLOPT_SSL_VERIFYPEER, insecure ? 0L : 1L);

    if (ca)
        curl_easy_setopt(connection->easy.get(), CURLOPT_CAINFO, ca->c_str());

    if (cert)
        curl_easy_setopt(connection->easy.get(), CURLOPT_SSLCERT, cert->c_str());

    if (privateKey)
        curl_easy_setopt(connection->easy.get(), CURLOPT_SSLKEY, privateKey->c_str());

    if (password)
        curl_easy_setopt(connection->easy.get(), CURLOPT_KEYPASSWD, password->c_str());

    for (const auto &hook: hooks) {
        EXPECT(hook(*connection));
    }

    return connection;
}

asyncio::task::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::perform(std::unique_ptr<Connection> connection) {
    CO_EXPECT(expected([&] {
        return curl_multi_add_handle(mCore->multi.get(), connection->easy.get());
    }));

    DEFER(
        if (connection)
            curl_multi_remove_handle(mCore->multi.get(), connection->easy.get());
    );

    CO_EXPECT(co_await task::Cancellable{
        connection->promise.getFuture(),
        [&promise = connection->promise]() -> std::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return std::unexpected{task::Error::WILL_BE_DONE};

            promise.reject(task::Error::CANCELLED);
            return {};
        }
    });

    co_return Response{this, std::move(connection)};
}

asyncio::task::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(std::string method, const URL url, const std::optional<Options> options) {
    auto connection = prepare(std::move(method), url, options);
    CO_EXPECT(connection);
    co_return co_await perform(*std::move(connection));
}

asyncio::task::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(
    std::string method,
    const URL url,
    const std::optional<Options> options,
    const std::string payload
) {
    auto connection = prepare(std::move(method), url, options);
    CO_EXPECT(connection);

    curl_easy_setopt(connection.value()->easy.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.length()));
    curl_easy_setopt(connection.value()->easy.get(), CURLOPT_COPYPOSTFIELDS, payload.c_str());

    co_return co_await perform(*std::move(connection));
}

asyncio::task::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(
    std::string method,
    const URL url,
    const std::optional<Options> options,
    std::map<std::string, std::string> payload
) {
    auto connection = prepare(std::move(method), url, options);
    CO_EXPECT(connection);

    curl_easy_setopt(
        connection.value()->easy.get(),
        CURLOPT_COPYPOSTFIELDS,
        to_string(fmt::join(
            payload | std::views::transform([](const auto &it) { return it.first + "=" + it.second; }),
            "&"
        )).c_str()
    );

    co_return co_await perform(*std::move(connection));
}

asyncio::task::Task<asyncio::http::Response, std::error_code>
asyncio::http::Requests::request(
    std::string method,
    const URL url,
    const std::optional<Options> options,
    std::map<std::string, std::variant<std::string, std::filesystem::path>> payload
) {
    auto connection = prepare(std::move(method), url, options);
    CO_EXPECT(connection);

    std::unique_ptr<curl_mime, decltype(&curl_mime_free)> form{
        curl_mime_init(connection.value()->easy.get()),
        curl_mime_free
    };

    for (const auto &[k, v]: payload) {
        const auto field = curl_mime_addpart(form.get());
        curl_mime_name(field, k.c_str());

        if (std::holds_alternative<std::string>(v)) {
            CO_EXPECT(expected([&] {
                return curl_mime_data(field, std::get<std::string>(v).c_str(), CURL_ZERO_TERMINATED);
            }));
            continue;
        }

        CO_EXPECT(expected([&] {
            return curl_mime_filedata(field, std::get<std::filesystem::path>(v).string().c_str());
        }));
    }

    curl_easy_setopt(connection.value()->easy.get(), CURLOPT_MIMEPOST, form.get());

    connection.value()->defers.emplace_back([form = form.release()] {
        curl_mime_free(form);
    });

    co_return co_await perform(*std::move(connection));
}

DEFINE_ERROR_CATEGORY_INSTANCES(asyncio::http::Requests::CURLError, asyncio::http::Requests::CURLMError)

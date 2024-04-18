#include <asyncio/net/stream.h>
#include <asyncio/net/dns.h>
#include <asyncio/promise.h>
#include <zero/defer.h>
#include <cassert>

#if __unix__ || __APPLE__
#include <sys/un.h>
#endif

asyncio::net::stream::Buffer::Buffer(
    std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev,
    const std::size_t capacity
) : ev::Buffer(std::move(bev), capacity) {
}

tl::expected<asyncio::net::stream::Buffer, std::error_code>
asyncio::net::stream::Buffer::make(const FileDescriptor fd, const std::size_t capacity, const bool own) {
    bufferevent *bev = bufferevent_socket_new(getEventLoop()->base(), fd, own ? BEV_OPT_CLOSE_ON_FREE : 0);

    if (!bev)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    return Buffer{{bev, bufferevent_free}, capacity};
}

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::stream::Buffer::localAddress() const {
    const FileDescriptor fd = this->fd();

    if (fd == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    return addressFrom(fd, false);
}

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::stream::Buffer::remoteAddress() const {
    const FileDescriptor fd = this->fd();

    if (fd == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    return addressFrom(fd, true);
}

asyncio::net::stream::Acceptor::Acceptor(evconnlistener *listener) : mListener(listener, evconnlistener_free) {
    evconnlistener_set_cb(
        mListener.get(),
        [](evconnlistener *, evutil_socket_t fd, sockaddr *, int, void *arg) {
            std::exchange(static_cast<Acceptor *>(arg)->mPromise, std::nullopt)->resolve(fd);
        },
        this
    );
}

asyncio::net::stream::Acceptor::Acceptor(Acceptor &&rhs) noexcept : mListener(std::move(rhs.mListener)) {
    assert(!rhs.mPromise);
    evconnlistener_set_cb(
        mListener.get(),
        [](evconnlistener *, evutil_socket_t fd, sockaddr *, int, void *arg) {
            std::exchange(static_cast<Acceptor *>(arg)->mPromise, std::nullopt)->resolve(fd);
        },
        this
    );
}

asyncio::net::stream::Acceptor &asyncio::net::stream::Acceptor::operator=(Acceptor &&rhs) noexcept {
    assert(!rhs.mPromise);
    mListener = std::move(rhs.mListener);

    evconnlistener_set_cb(
        mListener.get(),
        [](evconnlistener *, evutil_socket_t fd, sockaddr *, int, void *arg) {
            std::exchange(static_cast<Acceptor *>(arg)->mPromise, std::nullopt)->resolve(fd);
        },
        this
    );

    return *this;
}

asyncio::net::stream::Acceptor::~Acceptor() {
    assert(!mPromise);
}

zero::async::coroutine::Task<asyncio::FileDescriptor, std::error_code> asyncio::net::stream::Acceptor::fd() {
    if (!mListener)
        co_return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (mPromise)
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    co_return co_await zero::async::coroutine::Cancellable{
        zero::async::promise::chain<FileDescriptor, std::error_code>([this](auto promise) {
            mPromise.emplace(std::move(promise));
            evconnlistener_enable(mListener.get());
        }).finally([this] {
            evconnlistener_disable(mListener.get());
        }),
        [this]() -> tl::expected<void, std::error_code> {
            if (!mPromise)
                return tl::unexpected(make_error_code(std::errc::operation_not_supported));

            std::exchange(mPromise, std::nullopt)->reject(make_error_code(std::errc::operation_canceled));
            return {};
        }
    };
}

tl::expected<void, std::error_code> asyncio::net::stream::Acceptor::close() {
    if (!mListener)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    if (auto promise = std::exchange(mPromise, std::nullopt); promise)
        promise->reject(make_error_code(std::errc::bad_file_descriptor));

    mListener.reset();
    return {};
}

asyncio::net::stream::Listener::Listener(evconnlistener *listener) : Acceptor(listener) {
}

zero::async::coroutine::Task<asyncio::net::stream::Buffer, std::error_code>
asyncio::net::stream::Listener::accept() {
    const auto result = co_await fd();
    CO_EXPECT(result);
    co_return Buffer::make(*result);
}

tl::expected<asyncio::net::stream::Listener, std::error_code> asyncio::net::stream::listen(const Address &address) {
    const auto socketAddress = socketAddressFrom(address);
    EXPECT(socketAddress);

    evconnlistener *listener = evconnlistener_new_bind(
        getEventLoop()->base(),
        nullptr,
        nullptr,
        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE | LEV_OPT_DISABLED,
        -1,
        socketAddress->first.get(),
#ifdef _WIN32
        socketAddress->second
#else
        static_cast<int>(socketAddress->second)
#endif
    );

    if (!listener)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    return Listener{listener};
}

tl::expected<asyncio::net::stream::Listener, std::error_code>
asyncio::net::stream::listen(std::span<const Address> addresses) {
    if (addresses.empty())
        return tl::unexpected(make_error_code(std::errc::invalid_argument));

    auto it = addresses.begin();

    while (true) {
        auto result = listen(*it);

        if (result)
            return *std::move(result);

        if (++it == addresses.end())
            return tl::unexpected(result.error());
    }
}

tl::expected<asyncio::net::stream::Listener, std::error_code>
asyncio::net::stream::listen(const std::string &ip, const unsigned short port) {
    const auto address = addressFrom(ip, port);
    EXPECT(address);
    return listen(*address);
}

zero::async::coroutine::Task<asyncio::net::stream::Buffer, std::error_code>
asyncio::net::stream::connect(const Address address) {
    if (std::holds_alternative<IPv4Address>(address)) {
        const auto [port, ip] = std::get<IPv4Address>(address);
        co_return co_await connect(zero::os::net::stringify(ip), port);
    }

    if (std::holds_alternative<IPv6Address>(address)) {
        const auto &[port, ip, zone] = std::get<IPv6Address>(address);
        co_return co_await connect(zero::os::net::stringify(ip), port);
    }
#if __unix__ || __APPLE__
    else if (std::holds_alternative<UnixAddress>(address)) {
        co_return co_await connect(std::get<UnixAddress>(address).path);
    }
#endif

    co_return tl::unexpected(make_error_code(std::errc::address_family_not_supported));
}

zero::async::coroutine::Task<asyncio::net::stream::Buffer, std::error_code>
asyncio::net::stream::connect(std::span<const Address> addresses) {
    if (addresses.empty())
        co_return tl::unexpected(make_error_code(std::errc::invalid_argument));

    auto it = addresses.begin();

    while (true) {
        auto result = co_await connect(*it);

        if (result)
            co_return *std::move(result);

        if (++it == addresses.end())
            co_return tl::unexpected(result.error());
    }
}

zero::async::coroutine::Task<asyncio::net::stream::Buffer, std::error_code>
asyncio::net::stream::connect(const std::string host, const unsigned short port) {
    const auto dnsBase = getEventLoop()->makeDNSBase();
    CO_EXPECT(dnsBase);

    bufferevent *bev = bufferevent_socket_new(getEventLoop()->base(), -1, BEV_OPT_CLOSE_ON_FREE);

    if (!bev)
        co_return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    DEFER(
        if (bev)
            bufferevent_free(bev);
    );

    Promise<void, std::error_code> promise;

    bufferevent_setcb(
        bev,
        nullptr,
        nullptr,
        [](bufferevent *b, const short what, void *arg) {
            const auto p = static_cast<Promise<void, std::error_code> *>(arg);
            assert(!p->isFulfilled());

            if ((what & BEV_EVENT_CONNECTED) == 0) {
                if (const int e = bufferevent_socket_get_dns_error(b)) {
                    p->reject(static_cast<dns::Error>(e));
                    return;
                }

                p->reject(EVUTIL_SOCKET_ERROR(), std::system_category());
                return;
            }

            p->resolve();
        },
        &promise
    );

    if (bufferevent_socket_connect_hostname(bev, dnsBase->get(), AF_UNSPEC, host.c_str(), port) < 0)
        co_return tl::unexpected(make_error_code(std::errc::invalid_argument));

    CO_EXPECT(co_await zero::async::coroutine::Cancellable{
        promise.getFuture(),
        [&]() -> tl::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return tl::unexpected(make_error_code(std::errc::operation_not_supported));

            bufferevent_free(std::exchange(bev, nullptr));
            promise.reject(make_error_code(std::errc::operation_canceled));
            return {};
        }
    });

    co_return Buffer{{std::exchange(bev, nullptr), bufferevent_free}, DEFAULT_BUFFER_CAPACITY};
}

#if __unix__ || __APPLE__
tl::expected<asyncio::net::stream::Listener, std::error_code> asyncio::net::stream::listen(const std::string &path) {
    if (path.empty())
        return tl::unexpected(make_error_code(std::errc::invalid_argument));

    sockaddr_un sa = {};
    socklen_t length = sizeof(sa_family_t) + path.length() + 1;

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path.c_str(), sizeof(sa.sun_path) - 1);

    if (path.front() == '@') {
        length--;
        sa.sun_path[0] = '\0';
    }

    evconnlistener *listener = evconnlistener_new_bind(
        getEventLoop()->base(),
        nullptr,
        nullptr,
        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_DISABLED,
        -1,
        reinterpret_cast<const sockaddr *>(&sa),
        static_cast<int>(length)
    );

    if (!listener)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    return Listener{listener};
}

zero::async::coroutine::Task<asyncio::net::stream::Buffer, std::error_code>
asyncio::net::stream::connect(const std::string path) {
    if (path.empty())
        co_return tl::unexpected(make_error_code(std::errc::invalid_argument));

    sockaddr_un sa = {};
    socklen_t length = sizeof(sa_family_t) + path.length() + 1;

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path.c_str(), sizeof(sa.sun_path) - 1);

    if (path.front() == '@') {
        length--;
        sa.sun_path[0] = '\0';
    }

    bufferevent *bev = bufferevent_socket_new(getEventLoop()->base(), -1, BEV_OPT_CLOSE_ON_FREE);

    if (!bev)
        co_return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    DEFER(
        if (bev)
            bufferevent_free(bev);
    );

    Promise<void, std::error_code> promise;

    bufferevent_setcb(
        bev,
        nullptr,
        nullptr,
        [](bufferevent *, const short what, void *arg) {
            const auto p = static_cast<Promise<void, std::error_code> *>(arg);
            assert(!p->isFulfilled());

            if (p->isFulfilled())
                return;

            if ((what & BEV_EVENT_CONNECTED) == 0) {
                p->reject(EVUTIL_SOCKET_ERROR(), std::system_category());
                return;
            }

            p->resolve();
        },
        &promise
    );

    if (bufferevent_socket_connect(bev, reinterpret_cast<const sockaddr *>(&sa), static_cast<int>(length)) < 0)
        co_return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    CO_EXPECT(co_await zero::async::coroutine::Cancellable{
        promise.getFuture(),
        [&]() -> tl::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return tl::unexpected(make_error_code(std::errc::operation_not_supported));

            bufferevent_free(std::exchange(bev, nullptr));
            promise.reject(make_error_code(std::errc::operation_canceled));
            return {};
        }
    });

    co_return Buffer{{std::exchange(bev, nullptr), bufferevent_free}, DEFAULT_BUFFER_CAPACITY};
}
#endif

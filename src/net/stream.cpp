#include <asyncio/net/stream.h>
#include <asyncio/net/dns.h>
#include <asyncio/event_loop.h>
#include <asyncio/error.h>
#include <zero/os/net.h>
#include <cassert>

#if __unix__ || __APPLE__
#include <sys/un.h>
#endif

asyncio::net::stream::Buffer::Buffer(bufferevent *bev, const std::size_t capacity) : ev::Buffer(bev, capacity) {
}

asyncio::net::stream::Buffer::Buffer(
    std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev,
    const std::size_t capacity
) : ev::Buffer(std::move(bev), capacity) {
}

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::stream::Buffer::localAddress() {
    const FileDescriptor fd = this->fd();

    if (fd == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    return addressFrom(fd, false);
}

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::stream::Buffer::remoteAddress() {
    const FileDescriptor fd = this->fd();

    if (fd == INVALID_FILE_DESCRIPTOR)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    return addressFrom(fd, true);
}

tl::expected<asyncio::net::stream::Buffer, std::error_code>
asyncio::net::stream::makeBuffer(const FileDescriptor fd, const std::size_t capacity, const bool own) {
    bufferevent *bev = bufferevent_socket_new(getEventLoop()->base(), fd, own ? BEV_OPT_CLOSE_ON_FREE : 0);

    if (!bev)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return Buffer{bev, capacity};
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
            mPromise = promise;
            evconnlistener_enable(mListener.get());
        }).finally([this] {
            evconnlistener_disable(mListener.get());
        }),
        [this]() -> tl::expected<void, std::error_code> {
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
    auto result = CO_TRY(co_await fd());
    co_return makeBuffer(*result);
}

tl::expected<asyncio::net::stream::Listener, std::error_code> asyncio::net::stream::listen(const Address &address) {
    auto socketAddress = TRY(socketAddressFrom(address));

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
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

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
            return result;

        if (++it == addresses.end())
            return tl::unexpected(result.error());
    }
}

tl::expected<asyncio::net::stream::Listener, std::error_code>
asyncio::net::stream::listen(const std::string &ip, const unsigned short port) {
    const auto address = TRY(addressFrom(ip, port));
    return listen(*address);
}

zero::async::coroutine::Task<asyncio::net::stream::Buffer, std::error_code>
asyncio::net::stream::connect(const Address address) {
    if (const std::size_t index = address.index(); index == 0) {
        const auto [port, ip] = std::get<IPv4Address>(address);
        co_return std::move(co_await connect(zero::os::net::stringify(ip), port));
    }
    else if (index == 1) {
        const auto &[port, ip, zone] = std::get<IPv6Address>(address);
        co_return std::move(co_await connect(zero::os::net::stringify(ip), port));
    }
#if __unix__ || __APPLE__
    else if (index == 2) {
        co_return std::move(co_await connect(std::get<UnixAddress>(address).path));
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
        auto result = std::move(co_await connect(*it));

        if (result)
            co_return std::move(*result);

        if (++it == addresses.end())
            co_return tl::unexpected(result.error());
    }
}

zero::async::coroutine::Task<asyncio::net::stream::Buffer, std::error_code>
asyncio::net::stream::connect(const std::string host, const unsigned short port) {
    bufferevent *bev = bufferevent_socket_new(getEventLoop()->base(), -1, BEV_OPT_CLOSE_ON_FREE);

    if (!bev)
        co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    zero::async::promise::Promise<void, std::error_code> promise;
    const auto ctx = new zero::async::promise::Promise(promise);

    bufferevent_setcb(
        bev,
        nullptr,
        nullptr,
        [](bufferevent *b, const short what, void *arg) {
            const auto p = static_cast<zero::async::promise::Promise<void, std::error_code> *>(arg);

            if ((what & BEV_EVENT_CONNECTED) == 0) {
                if (const int e = bufferevent_socket_get_dns_error(b)) {
                    p->reject(static_cast<dns::Error>(e));
                    delete p;
                    return;
                }

                p->reject(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
                delete p;
                return;
            }

            p->resolve();
            delete p;
        },
        ctx
    );

    if (bufferevent_socket_connect_hostname(bev, getEventLoop()->dnsBase(), AF_UNSPEC, host.c_str(), port) < 0) {
        delete ctx;
        bufferevent_free(bev);
        co_return tl::unexpected(make_error_code(std::errc::invalid_argument));
    }

    if (const auto result = co_await zero::async::coroutine::Cancellable{
        promise,
        [=]() mutable -> tl::expected<void, std::error_code> {
            bufferevent_setcb(bev, nullptr, nullptr, nullptr, nullptr);
            promise.reject(make_error_code(std::errc::operation_canceled));
            delete ctx;
            return {};
        }
    }; !result) {
        bufferevent_free(bev);
        co_return tl::unexpected(result.error());
    }

    co_return Buffer{bev, DEFAULT_BUFFER_CAPACITY};
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
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

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
        co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    zero::async::promise::Promise<void, std::error_code> promise;
    const auto ctx = new zero::async::promise::Promise(promise);

    bufferevent_setcb(
        bev,
        nullptr,
        nullptr,
        [](bufferevent *, const short what, void *arg) {
            const auto p = static_cast<zero::async::promise::Promise<void, std::error_code> *>(arg);

            if ((what & BEV_EVENT_CONNECTED) == 0) {
                p->reject(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
                delete p;
                return;
            }

            p->resolve();
            delete p;
        },
        ctx
    );

    if (bufferevent_socket_connect(bev, reinterpret_cast<const sockaddr *>(&sa), static_cast<int>(length)) < 0) {
        delete ctx;
        bufferevent_free(bev);
        co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }

    if (const auto result = co_await zero::async::coroutine::Cancellable{
        promise,
        [=]() mutable -> tl::expected<void, std::error_code> {
            bufferevent_setcb(bev, nullptr, nullptr, nullptr, nullptr);
            promise.reject(make_error_code(std::errc::operation_canceled));
            delete ctx;
            return {};
        }
    }; !result) {
        bufferevent_free(bev);
        co_return tl::unexpected(result.error());
    }

    co_return Buffer{bev, DEFAULT_BUFFER_CAPACITY};
}
#endif

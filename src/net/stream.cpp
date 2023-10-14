#include <asyncio/net/stream.h>
#include <asyncio/net/dns.h>
#include <asyncio/event_loop.h>
#include <zero/os/net.h>
#include <cstring>
#include <cassert>

#if __unix__ || __APPLE__
#include <sys/un.h>
#endif

asyncio::net::stream::Buffer::Buffer(bufferevent *bev) : ev::Buffer(bev) {

}

asyncio::net::stream::Buffer::Buffer(std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev)
        : ev::Buffer(std::move(bev)) {

}

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::stream::Buffer::localAddress() {
    evutil_socket_t fd = this->fd();

    if (fd == EVUTIL_INVALID_SOCKET)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    return addressFrom(fd, false);
}

tl::expected<asyncio::net::Address, std::error_code> asyncio::net::stream::Buffer::remoteAddress() {
    evutil_socket_t fd = this->fd();

    if (fd == EVUTIL_INVALID_SOCKET)
        return tl::unexpected(make_error_code(std::errc::bad_file_descriptor));

    return addressFrom(fd, true);
}

tl::expected<asyncio::net::stream::Buffer, std::error_code>
asyncio::net::stream::makeBuffer(evutil_socket_t fd, bool own) {
    bufferevent *bev = bufferevent_socket_new(getEventLoop()->base(), fd, own ? BEV_OPT_CLOSE_ON_FREE : 0);

    if (!bev)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return Buffer{bev};
}

asyncio::net::stream::Acceptor::Acceptor(evconnlistener *listener) : mListener(listener, evconnlistener_free) {
    evconnlistener_set_cb(
            mListener.get(),
            [](evconnlistener *listener, evutil_socket_t fd, sockaddr *addr, int socklen, void *arg) {
                std::exchange(static_cast<Acceptor *>(arg)->mPromise, std::nullopt)->resolve(fd);
            },
            this
    );
}

asyncio::net::stream::Acceptor::Acceptor(asyncio::net::stream::Acceptor &&rhs) noexcept
        : mListener(std::move(rhs.mListener)) {
    assert(!mPromise);
    evconnlistener_set_cb(
            mListener.get(),
            [](evconnlistener *listener, evutil_socket_t fd, sockaddr *addr, int socklen, void *arg) {
                std::exchange(static_cast<Acceptor *>(arg)->mPromise, std::nullopt)->resolve(fd);
            },
            this
    );
}

asyncio::net::stream::Acceptor::~Acceptor() {
    assert(!mPromise);
}

zero::async::coroutine::Task<evutil_socket_t, std::error_code> asyncio::net::stream::Acceptor::fd() {
    if (!mListener)
        co_return tl::unexpected(Error::RESOURCE_DESTROYED);

    if (mPromise)
        co_return tl::unexpected(make_error_code(std::errc::operation_in_progress));

    co_return co_await zero::async::coroutine::Cancellable{
            zero::async::promise::chain<evutil_socket_t, std::error_code>([this](auto promise) {
                mPromise = promise;
                evconnlistener_enable(mListener.get());
            }).finally([this]() {
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
        return tl::unexpected(Error::RESOURCE_DESTROYED);

    auto promise = std::exchange(mPromise, std::nullopt);

    if (promise)
        promise->reject(Error::RESOURCE_DESTROYED);

    mListener.reset();
    return {};
}

asyncio::net::stream::Listener::Listener(evconnlistener *listener) : Acceptor(listener) {

}

zero::async::coroutine::Task<asyncio::net::stream::Buffer, std::error_code>
asyncio::net::stream::Listener::accept() {
    auto result = CO_TRY(co_await fd());
    co_return makeBuffer(*result, true);
}

tl::expected<asyncio::net::stream::Listener, std::error_code> asyncio::net::stream::listen(const Address &address) {
    auto socketAddress = TRY(socketAddressFrom(address));

    evconnlistener *listener = evconnlistener_new_bind(
            getEventLoop()->base(),
            nullptr,
            nullptr,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE | LEV_OPT_DISABLED,
            -1,
            (const sockaddr *) socketAddress->data(),
            (int) socketAddress->size()
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
asyncio::net::stream::listen(const std::string &ip, unsigned short port) {
    auto address = TRY(addressFrom(ip, port));
    return listen(*address);
}

zero::async::coroutine::Task<asyncio::net::stream::Buffer, std::error_code>
asyncio::net::stream::connect(Address address) {
    size_t index = address.index();

    if (index == 0) {
        IPv4Address ipv4 = std::get<IPv4Address>(address);
        co_return std::move(co_await connect(zero::os::net::stringify(ipv4.ip), ipv4.port));
    } else if (index == 1) {
        IPv6Address ipv6 = std::get<IPv6Address>(address);
        co_return std::move(co_await connect(zero::os::net::stringify(ipv6.ip), ipv6.port));
    } else if (index == 2) {
#if __unix__ || __APPLE__
        co_return std::move(co_await connect(std::get<UnixAddress>(address).path));
#endif
    }

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
asyncio::net::stream::connect(std::string host, unsigned short port) {
    bufferevent *bev = bufferevent_socket_new(getEventLoop()->base(), -1, BEV_OPT_CLOSE_ON_FREE);

    if (!bev)
        co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    zero::async::promise::Promise<void, std::error_code> promise;
    auto ctx = new zero::async::promise::Promise<void, std::error_code>(promise);

    bufferevent_setcb(
            bev,
            nullptr,
            nullptr,
            [](bufferevent *bev, short what, void *arg) {
                auto promise = static_cast<zero::async::promise::Promise<void, std::error_code> *>(arg);

                if ((what & BEV_EVENT_CONNECTED) == 0) {
                    if (int e = bufferevent_socket_get_dns_error(bev); e) {
                        promise->reject((dns::Error) e);
                        delete promise;
                        return;
                    }

                    promise->reject(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
                    delete promise;
                    return;
                }

                promise->resolve();
                delete promise;
            },
            ctx
    );

    if (bufferevent_socket_connect_hostname(bev, getEventLoop()->dnsBase(), AF_UNSPEC, host.c_str(), port) < 0) {
        delete ctx;
        bufferevent_free(bev);
        co_return tl::unexpected(make_error_code(std::errc::invalid_argument));
    }

    auto result = co_await zero::async::coroutine::Cancellable{
            promise,
            [=]() mutable -> tl::expected<void, std::error_code> {
                bufferevent_setcb(bev, nullptr, nullptr, nullptr, nullptr);
                promise.reject(make_error_code(std::errc::operation_canceled));
                delete ctx;
                return {};
            }
    };

    if (!result) {
        bufferevent_free(bev);
        co_return tl::unexpected(result.error());
    }

    co_return Buffer{bev};
}

#if __unix__ || __APPLE__
tl::expected<asyncio::net::stream::Listener, std::error_code> asyncio::net::stream::listen(const std::string &path) {
    sockaddr_un sa = {};

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path.c_str(), sizeof(sa.sun_path) - 1);

    evconnlistener *listener = evconnlistener_new_bind(
            getEventLoop()->base(),
            nullptr,
            nullptr,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_DISABLED,
            -1,
            (const sockaddr *) &sa,
            sizeof(sa)
    );

    if (!listener)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    return Listener{listener};
}

zero::async::coroutine::Task<asyncio::net::stream::Buffer, std::error_code>
asyncio::net::stream::connect(std::string path) {
    sockaddr_un sa = {};

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path.c_str(), sizeof(sa.sun_path) - 1);

    bufferevent *bev = bufferevent_socket_new(getEventLoop()->base(), -1, BEV_OPT_CLOSE_ON_FREE);

    if (!bev)
        co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    zero::async::promise::Promise<void, std::error_code> promise;
    auto ctx = new zero::async::promise::Promise<void, std::error_code>(promise);

    bufferevent_setcb(
            bev,
            nullptr,
            nullptr,
            [](bufferevent *bev, short what, void *arg) {
                auto promise = static_cast<zero::async::promise::Promise<void, std::error_code> *>(arg);

                if ((what & BEV_EVENT_CONNECTED) == 0) {
                    promise->reject(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
                    delete promise;
                    return;
                }

                promise->resolve();
                delete promise;
            },
            ctx
    );

    if (bufferevent_socket_connect(bev, (const sockaddr *) &sa, sizeof(sa)) < 0) {
        delete ctx;
        bufferevent_free(bev);
        co_return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }

    auto result = co_await zero::async::coroutine::Cancellable{
            promise,
            [=]() mutable -> tl::expected<void, std::error_code> {
                bufferevent_setcb(bev, nullptr, nullptr, nullptr, nullptr);
                promise.reject(make_error_code(std::errc::operation_canceled));
                delete ctx;
                return {};
            }
    };

    if (!result) {
        bufferevent_free(bev);
        co_return tl::unexpected(result.error());
    }

    co_return Buffer{bev};
}
#endif

#include <asyncio/net/dgram.h>
#include <asyncio/net/dns.h>

asyncio::net::UDPSocket::UDPSocket(uv::Handle<uv_udp_t> udp) : mUDP{std::move(udp)} {
}

std::expected<asyncio::net::UDPSocket, std::error_code> asyncio::net::UDPSocket::make() {
    auto udp = std::make_unique<uv_udp_t>();

    EXPECT(uv::expected([&] {
        return uv_udp_init(getEventLoop()->raw(), udp.get());
    }));

    return UDPSocket{uv::Handle{std::move(udp)}};
}

std::expected<asyncio::net::UDPSocket, std::error_code>
asyncio::net::UDPSocket::bind(const SocketAddress &address) {
    auto udp = make();
    EXPECT(udp);

    EXPECT(uv::expected([&] {
        return uv_udp_bind(udp->mUDP.raw(), address.first.get(), 0);
    }));

    return *std::move(udp);
}

std::expected<asyncio::net::UDPSocket, std::error_code>
asyncio::net::UDPSocket::connect(const SocketAddress &address) {
    auto udp = make();
    EXPECT(udp);

    EXPECT(uv::expected([&] {
        return uv_udp_connect(udp->mUDP.raw(), address.first.get());
    }));

    return *std::move(udp);
}

std::expected<asyncio::net::UDPSocket, std::error_code> asyncio::net::UDPSocket::from(const uv_os_sock_t socket) {
    auto udp = make();
    EXPECT(udp);

    EXPECT(uv::expected([&] {
        return uv_udp_open(udp->mUDP.raw(), socket);
    }));

    return *std::move(udp);
}

std::expected<asyncio::net::UDPSocket, std::error_code>
asyncio::net::UDPSocket::bind(const std::string &ip, const std::uint16_t port) {
    const auto address = ipAddressFrom(ip, port);
    EXPECT(address);

    auto socketAddress = std::visit(
        [](const auto &arg) {
            return socketAddressFrom(arg);
        },
        *address
    );
    EXPECT(socketAddress);

    return bind(*std::move(socketAddress));
}

std::expected<asyncio::net::UDPSocket, std::error_code>
asyncio::net::UDPSocket::bind(const IPv4Address &address) {
    auto socketAddress = socketAddressFrom(address);
    EXPECT(socketAddress);
    return bind(*std::move(socketAddress));
}

std::expected<asyncio::net::UDPSocket, std::error_code>
asyncio::net::UDPSocket::bind(const IPv6Address &address) {
    auto socketAddress = socketAddressFrom(address);
    EXPECT(socketAddress);
    return bind(*std::move(socketAddress));
}

asyncio::task::Task<asyncio::net::UDPSocket, std::error_code>
asyncio::net::UDPSocket::connect(const std::string host, const std::uint16_t port) {
    const auto addresses = co_await dns::getAddressInfo(
        host,
        std::to_string(port),
        addrinfo{
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_DGRAM
        }
    );
    CO_EXPECT(addresses);
    assert(!addresses->empty());

    auto it = addresses->begin();

    while (true) {
        auto socketAddress = socketAddressFrom(*it);
        CO_EXPECT(socketAddress);

        auto result = connect(*std::move(socketAddress));

        if (result)
            co_return *std::move(result);

        if (++it == addresses->end())
            co_return std::unexpected{result.error()};
    }
}

std::expected<asyncio::net::UDPSocket, std::error_code>
asyncio::net::UDPSocket::connect(const IPv4Address &address) {
    auto socketAddress = socketAddressFrom(address);
    EXPECT(socketAddress);
    return connect(*std::move(socketAddress));
}

std::expected<asyncio::net::UDPSocket, std::error_code>
asyncio::net::UDPSocket::connect(const IPv6Address &address) {
    auto socketAddress = socketAddressFrom(address);
    EXPECT(socketAddress);
    return connect(*std::move(socketAddress));
}

asyncio::FileDescriptor asyncio::net::UDPSocket::fd() const {
    const auto fd = mUDP.fd();
    assert(fd);
    return *fd;
}

std::expected<asyncio::net::Address, std::error_code> asyncio::net::UDPSocket::localAddress() const {
    sockaddr_storage storage{};
    auto length = static_cast<int>(sizeof(sockaddr_storage));

    EXPECT(uv::expected([&] {
        return uv_udp_getsockname(mUDP.raw(), reinterpret_cast<sockaddr *>(&storage), &length);
    }));

    return addressFrom(reinterpret_cast<const sockaddr *>(&storage), length);
}

std::expected<asyncio::net::Address, std::error_code> asyncio::net::UDPSocket::remoteAddress() const {
    sockaddr_storage storage{};
    auto length = static_cast<int>(sizeof(sockaddr_storage));

    EXPECT(uv::expected([&] {
        return uv_udp_getpeername(mUDP.raw(), reinterpret_cast<sockaddr *>(&storage), &length);
    }));

    return addressFrom(reinterpret_cast<const sockaddr *>(&storage), length);
}

std::expected<void, std::error_code>
asyncio::net::UDPSocket::setMembership(
    const std::string &multicastAddress,
    const std::string &interfaceAddress,
    const Membership membership
) {
    EXPECT(uv::expected([&] {
        return uv_udp_set_membership(
            mUDP.raw(),
            multicastAddress.c_str(),
            interfaceAddress.c_str(),
            static_cast<uv_membership>(membership)
        );
    }));
    return {};
}

std::expected<void, std::error_code> asyncio::net::UDPSocket::setSourceMembership(
    const std::string &multicastAddress,
    const std::string &interfaceAddress,
    const std::string &sourceAddress,
    const Membership membership
) {
    EXPECT(uv::expected([&] {
        return uv_udp_set_source_membership(
            mUDP.raw(),
            multicastAddress.c_str(),
            interfaceAddress.c_str(),
            sourceAddress.c_str(),
            static_cast<uv_membership>(membership)
        );
    }));
    return {};
}

std::expected<void, std::error_code> asyncio::net::UDPSocket::setMulticastLoop(const bool on) {
    EXPECT(uv::expected([&] {
        return uv_udp_set_multicast_loop(mUDP.raw(), on);
    }));
    return {};
}

std::expected<void, std::error_code> asyncio::net::UDPSocket::setMulticastTTL(const int ttl) {
    EXPECT(uv::expected([&] {
        return uv_udp_set_multicast_ttl(mUDP.raw(), ttl);
    }));
    return {};
}

std::expected<void, std::error_code>
asyncio::net::UDPSocket::setMulticastInterface(const std::string &interfaceAddress) {
    EXPECT(uv::expected([&] {
        return uv_udp_set_multicast_interface(mUDP.raw(), interfaceAddress.c_str());
    }));
    return {};
}

std::expected<void, std::error_code> asyncio::net::UDPSocket::setBroadcast(const bool on) {
    EXPECT(uv::expected([&] {
        return uv_udp_set_broadcast(mUDP.raw(), on);
    }));
    return {};
}

std::expected<void, std::error_code> asyncio::net::UDPSocket::setTTL(const int ttl) {
    EXPECT(uv::expected([&] {
        return uv_udp_set_ttl(mUDP.raw(), ttl);
    }));
    return {};
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::UDPSocket::read(const std::span<std::byte> data) {
    return readFrom(data).transform([](const auto &result) {
        return result.first;
    });
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::UDPSocket::write(const std::span<const std::byte> data) {
    Promise<void, std::error_code> promise;
    uv_udp_send_t request{.data = &promise};

    CO_EXPECT(uv::expected([&] {
        uv_buf_t buffer;

        buffer.base = reinterpret_cast<char *>(const_cast<std::byte *>(data.data()));
        buffer.len = static_cast<decltype(uv_buf_t::len)>(data.size());

        return uv_udp_send(
            &request,
            mUDP.raw(),
            &buffer,
            1,
            nullptr,
            [](auto *req, const int status) {
                const auto p = static_cast<Promise<void, std::error_code> *>(req->data);

                if (status < 0) {
                    p->reject(static_cast<uv::Error>(status));
                    return;
                }

                p->resolve();
            }
        );
    }));

    CO_EXPECT(co_await promise.getFuture());
    co_return data.size();
}

asyncio::task::Task<std::pair<std::size_t, asyncio::net::Address>, std::error_code>
asyncio::net::UDPSocket::readFrom(const std::span<std::byte> data) {
    struct Context {
        std::span<std::byte> data;
        Promise<std::pair<std::size_t, Address>, std::error_code> promise;
    };

    Context context{data};
    mUDP->data = &context;

    CO_EXPECT(uv::expected([&] {
        return uv_udp_recv_start(
            mUDP.raw(),
            [](auto *handle, const size_t, uv_buf_t *buf) {
                const auto span = static_cast<const Context *>(handle->data)->data;
                buf->base = reinterpret_cast<char *>(span.data());
                buf->len = static_cast<decltype(uv_buf_t::len)>(span.size());
            },
            [](auto *handle, const ssize_t n, const uv_buf_t *, const sockaddr *addr, const unsigned) {
                uv_udp_recv_stop(handle);
                auto &promise = static_cast<Context *>(handle->data)->promise;

                if (n < 0) {
                    promise.reject(static_cast<uv::Error>(n));
                    return;
                }

                assert(addr);
                auto address = addressFrom(addr, 0);

                if (!address) {
                    promise.reject(address.error());
                    return;
                }

                promise.resolve(static_cast<std::size_t>(n), *std::move(address));
            }
        );
    }));

    co_return co_await task::Cancellable{
        context.promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            if (context.promise.isFulfilled())
                return std::unexpected{task::Error::WILL_BE_DONE};

            uv_udp_recv_stop(mUDP.raw());
            context.promise.reject(task::Error::CANCELLED);
            return {};
        }
    };
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::UDPSocket::writeTo(const std::span<const std::byte> data, const Address address) {
    const auto socketAddress = socketAddressFrom(address);
    CO_EXPECT(socketAddress);

    Promise<void, std::error_code> promise;
    uv_udp_send_t request{.data = &promise};

    CO_EXPECT(uv::expected([&] {
        uv_buf_t buffer;

        buffer.base = reinterpret_cast<char *>(const_cast<std::byte *>(data.data()));
        buffer.len = static_cast<decltype(uv_buf_t::len)>(data.size());

        return uv_udp_send(
            &request,
            mUDP.raw(),
            &buffer,
            1,
            socketAddress->first.get(),
            [](auto *req, const int status) {
                const auto p = static_cast<Promise<void, std::error_code> *>(req->data);

                if (status < 0) {
                    p->reject(static_cast<uv::Error>(status));
                    return;
                }

                p->resolve();
            }
        );
    }));

    CO_EXPECT(co_await promise.getFuture());
    co_return data.size();
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::UDPSocket::writeTo(const std::span<const std::byte> data, std::string host, const std::uint16_t port) {
    const auto addresses = co_await dns::getAddressInfo(
        std::move(host),
        std::to_string(port),
        addrinfo{
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_DGRAM
        }
    );
    CO_EXPECT(addresses);
    assert(!addresses->empty());

    auto it = addresses->begin();

    while (true) {
        const auto result = co_await writeTo(data, *it);

        if (result)
            co_return *result;

        if (++it == addresses->end())
            co_return std::unexpected{result.error()};
    }
}

asyncio::task::Task<void, std::error_code> asyncio::net::UDPSocket::close() {
    const auto handle = mUDP.release();

    Promise<void> promise;
    handle->data = &promise;

    uv_close(
        reinterpret_cast<uv_handle_t *>(handle.get()),
        [](auto *h) {
            static_cast<Promise<void> *>(h->data)->resolve();
        }
    );

    co_await promise.getFuture();
    co_return {};
}

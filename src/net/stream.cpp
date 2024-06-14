#include <asyncio/net/stream.h>
#include <asyncio/net/dns.h>
#include <asyncio/promise.h>

asyncio::net::TCPStream::TCPStream(uv::Handle<uv_stream_t> stream) : Stream(std::move(stream)) {
}

// TODO
// except for MSVC, adding const will fail to compile.
// ReSharper disable once CppParameterMayBeConst
zero::async::coroutine::Task<asyncio::net::TCPStream, std::error_code>
asyncio::net::TCPStream::connect(SocketAddress address) {
    auto tcp = std::make_unique<uv_tcp_t>();

    CO_EXPECT(uv::expected([&] {
        return uv_tcp_init(getEventLoop()->raw(), tcp.get());
    }));

    TCPStream stream(uv::Handle{std::unique_ptr<uv_stream_t>{reinterpret_cast<uv_stream_t *>(tcp.release())}});

    Promise<void, std::error_code> promise;
    uv_connect_t request = {.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_tcp_connect(
            &request,
            reinterpret_cast<uv_tcp_t *>(stream.mStream.raw()),
            address.first.get(),
            [](const auto handle, const int status) {
                const auto p = static_cast<Promise<void, std::error_code> *>(handle->data);

                if (status < 0) {
                    p->reject(static_cast<uv::Error>(status));
                    return;
                }

                p->resolve();
            }
        );
    }));

    CO_EXPECT(co_await promise.getFuture());
    co_return std::move(stream);
}

zero::async::coroutine::Task<asyncio::net::TCPStream, std::error_code>
asyncio::net::TCPStream::connect(const std::string host, const unsigned short port) {
    addrinfo hints = {};

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const auto addresses = co_await dns::getAddressInfo(host, std::to_string(port), hints);
    CO_EXPECT(addresses);
    assert(!addresses->empty());

    auto it = addresses->begin();

    while (true) {
        auto socketAddress = socketAddressFrom(*it);
        CO_EXPECT(socketAddress);

        auto result = co_await connect(*std::move(socketAddress));

        if (result)
            co_return *std::move(result);

        if (++it == addresses->end())
            co_return std::unexpected(result.error());
    }
}

zero::async::coroutine::Task<asyncio::net::TCPStream, std::error_code>
asyncio::net::TCPStream::connect(const IPv4Address address) {
    auto socketAddress = socketAddressFrom(address);
    CO_EXPECT(socketAddress);
    co_return co_await connect(*std::move(socketAddress));
}

zero::async::coroutine::Task<asyncio::net::TCPStream, std::error_code>
asyncio::net::TCPStream::connect(const IPv6Address address) {
    auto socketAddress = socketAddressFrom(address);
    CO_EXPECT(socketAddress);
    co_return co_await connect(*std::move(socketAddress));
}

std::expected<asyncio::net::Address, std::error_code> asyncio::net::TCPStream::localAddress() const {
    sockaddr_storage storage = {};
    int length = sizeof(sockaddr_storage);

    EXPECT(uv::expected([&] {
        return uv_tcp_getsockname(
            reinterpret_cast<const uv_tcp_t *>(mStream.raw()),
            reinterpret_cast<sockaddr *>(&storage),
            &length
        );
    }));

    return addressFrom(reinterpret_cast<const sockaddr *>(&storage), length);
}

std::expected<asyncio::net::Address, std::error_code> asyncio::net::TCPStream::remoteAddress() const {
    sockaddr_storage storage = {};
    int length = sizeof(sockaddr_storage);

    EXPECT(uv::expected([&] {
        return uv_tcp_getpeername(
            reinterpret_cast<const uv_tcp_t *>(mStream.raw()),
            reinterpret_cast<sockaddr *>(&storage),
            &length
        );
    }));

    return addressFrom(reinterpret_cast<const sockaddr *>(&storage), length);
}

zero::async::coroutine::Task<std::pair<std::size_t, asyncio::net::Address>, std::error_code>
asyncio::net::TCPStream::readFrom(const std::span<std::byte> data) {
    auto remote = remoteAddress();
    CO_EXPECT(remote);

    auto n = co_await read(data);
    CO_EXPECT(n);

    co_return std::pair{*n, *std::move(remote)};
}

zero::async::coroutine::Task<std::size_t, std::error_code>
asyncio::net::TCPStream::writeTo(const std::span<const std::byte> data, const Address address) {
    co_return co_await write(data);
}

zero::async::coroutine::Task<void, std::error_code> asyncio::net::TCPStream::close() {
    mStream.close();
    co_return {};
}

asyncio::net::TCPListener::TCPListener(Listener listener) : mListener(std::move(listener)) {
}

std::expected<asyncio::net::TCPListener, std::error_code>
asyncio::net::TCPListener::listen(const SocketAddress &address) {
    auto tcp = std::make_unique<uv_tcp_t>();

    EXPECT(uv::expected([&] {
        return uv_tcp_init(getEventLoop()->raw(), tcp.get());
    }));

    EXPECT(uv::expected([&] {
        return uv_tcp_bind(tcp.get(), address.first.get(), 0);
    }));

    auto listener = Listener::make(
        uv::Handle{std::unique_ptr<uv_stream_t>{reinterpret_cast<uv_stream_t *>(tcp.release())}}
    );
    EXPECT(listener);

    return TCPListener{*std::move(listener)};
}

std::expected<asyncio::net::TCPListener, std::error_code>
asyncio::net::TCPListener::listen(const std::string &ip, const unsigned short port) {
    const auto address = addressFrom(ip, port);
    EXPECT(address);
    auto socketAddress = socketAddressFrom(*address);
    EXPECT(socketAddress);
    return listen(*std::move(socketAddress));
}

std::expected<asyncio::net::TCPListener, std::error_code>
asyncio::net::TCPListener::listen(const IPv4Address &address) {
    auto socketAddress = socketAddressFrom(address);
    EXPECT(socketAddress);
    return listen(*std::move(socketAddress));
}

std::expected<asyncio::net::TCPListener, std::error_code>
asyncio::net::TCPListener::listen(const IPv6Address &address) {
    auto socketAddress = socketAddressFrom(address);
    EXPECT(socketAddress);
    return listen(*std::move(socketAddress));
}

zero::async::coroutine::Task<asyncio::net::TCPStream, std::error_code>
asyncio::net::TCPListener::accept() {
    auto tcp = std::make_unique<uv_tcp_t>();

    CO_EXPECT(uv::expected([&] {
        return uv_tcp_init(getEventLoop()->raw(), tcp.get());
    }));

    CO_EXPECT(co_await mListener.accept(reinterpret_cast<uv_stream_t *>(tcp.get())));
    co_return TCPStream{uv::Handle{std::unique_ptr<uv_stream_t>{reinterpret_cast<uv_stream_t *>(tcp.release())}}};
}

#include <asyncio/net/stream.h>
#include <asyncio/net/dns.h>

#ifdef _WIN32
#include <zero/os/nt/error.h>
#elif __linux__
#include <zero/os/unix/error.h>
#elif __APPLE__
#include <unistd.h>
#include <sys/un.h>
#include <zero/os/unix/error.h>
#endif

asyncio::net::TCPStream::TCPStream(Stream stream) : mStream(std::move(stream)) {
}

// TODO
// except for MSVC, adding const will fail to compile.
// ReSharper disable once CppParameterMayBeConst
asyncio::task::Task<asyncio::net::TCPStream, std::error_code>
asyncio::net::TCPStream::connect(SocketAddress address) {
    std::unique_ptr<uv_tcp_t, decltype(&std::free)> tcp(
        static_cast<uv_tcp_t *>(std::malloc(sizeof(uv_tcp_t))),
        std::free
    );

    if (!tcp)
        co_return std::unexpected(std::error_code(errno, std::generic_category()));

    CO_EXPECT(uv::expected([&] {
        return uv_tcp_init(getEventLoop()->raw(), tcp.get());
    }));

    Stream stream(
        uv::Handle{
            std::unique_ptr<uv_stream_t, decltype(&std::free)>{
                reinterpret_cast<uv_stream_t *>(tcp.release()),
                std::free
            }
        }
    );

    Promise<void, std::error_code> promise;
    uv_connect_t request = {.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_tcp_connect(
            &request,
            reinterpret_cast<uv_tcp_t *>(stream.handle().raw()),
            address.first.get(),
            // ReSharper disable once CppParameterMayBeConstPtrOrRef
            [](uv_connect_t *req, const int status) {
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
    co_return TCPStream{std::move(stream)};
}

std::expected<asyncio::net::TCPStream, std::error_code> asyncio::net::TCPStream::from(const uv_os_sock_t socket) {
    std::unique_ptr<uv_tcp_t, decltype(&std::free)> tcp(
        static_cast<uv_tcp_t *>(std::malloc(sizeof(uv_tcp_t))),
        std::free
    );

    if (!tcp)
        return std::unexpected(std::error_code(errno, std::generic_category()));

    EXPECT(uv::expected([&] {
        return uv_tcp_init(getEventLoop()->raw(), tcp.get());
    }));

    uv::Handle handle(
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(tcp.release()),
            std::free
        }
    );

    EXPECT(uv::expected([&] {
        return uv_tcp_open(reinterpret_cast<uv_tcp_t *>(handle.raw()), socket);
    }));

    return TCPStream{Stream{std::move(handle)}};
}

asyncio::task::Task<asyncio::net::TCPStream, std::error_code>
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

asyncio::task::Task<asyncio::net::TCPStream, std::error_code>
asyncio::net::TCPStream::connect(const IPv4Address address) {
    auto socketAddress = socketAddressFrom(address);
    CO_EXPECT(socketAddress);
    co_return co_await connect(*std::move(socketAddress));
}

asyncio::task::Task<asyncio::net::TCPStream, std::error_code>
asyncio::net::TCPStream::connect(const IPv6Address address) {
    auto socketAddress = socketAddressFrom(address);
    CO_EXPECT(socketAddress);
    co_return co_await connect(*std::move(socketAddress));
}

asyncio::FileDescriptor asyncio::net::TCPStream::fd() const {
    const auto fd = mStream.handle().fd();
    assert(fd);
    return *fd;
}

std::expected<asyncio::net::Address, std::error_code> asyncio::net::TCPStream::localAddress() const {
    sockaddr_storage storage = {};
    int length = sizeof(sockaddr_storage);

    EXPECT(uv::expected([&] {
        return uv_tcp_getsockname(
            reinterpret_cast<const uv_tcp_t *>(mStream.handle().raw()),
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
            reinterpret_cast<const uv_tcp_t *>(mStream.handle().raw()),
            reinterpret_cast<sockaddr *>(&storage),
            &length
        );
    }));

    return addressFrom(reinterpret_cast<const sockaddr *>(&storage), length);
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::TCPStream::read(const std::span<std::byte> data) {
    co_return co_await mStream.read(data);
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::TCPStream::write(const std::span<const std::byte> data) {
    co_return co_await mStream.write(data);
}

asyncio::task::Task<std::pair<std::size_t, asyncio::net::Address>, std::error_code>
asyncio::net::TCPStream::readFrom(const std::span<std::byte> data) {
    auto remote = remoteAddress();
    CO_EXPECT(remote);

    auto n = co_await read(data);
    CO_EXPECT(n);

    co_return std::pair{*n, *std::move(remote)};
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::TCPStream::writeTo(const std::span<const std::byte> data, const Address) {
    co_return co_await write(data);
}

asyncio::task::Task<void, std::error_code> asyncio::net::TCPStream::close() {
    mStream.handle().close();
    co_return {};
}

asyncio::net::TCPListener::TCPListener(Listener listener) : mListener(std::move(listener)) {
}

std::expected<asyncio::net::TCPListener, std::error_code>
asyncio::net::TCPListener::listen(const SocketAddress &address) {
    std::unique_ptr<uv_tcp_t, decltype(&std::free)> tcp(
        static_cast<uv_tcp_t *>(std::malloc(sizeof(uv_tcp_t))),
        std::free
    );

    if (!tcp)
        return std::unexpected(std::error_code(errno, std::generic_category()));

    EXPECT(uv::expected([&] {
        return uv_tcp_init(getEventLoop()->raw(), tcp.get());
    }));

    uv::Handle handle(
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(tcp.release()),
            std::free
        }
    );

    EXPECT(uv::expected([&] {
        return uv_tcp_bind(reinterpret_cast<uv_tcp_t *>(handle.raw()), address.first.get(), 0);
    }));

    auto listener = Listener::make(std::move(handle));
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

asyncio::task::Task<asyncio::net::TCPStream, std::error_code>
asyncio::net::TCPListener::accept() {
    std::unique_ptr<uv_tcp_t, decltype(&std::free)> tcp(
        static_cast<uv_tcp_t *>(std::malloc(sizeof(uv_tcp_t))),
        std::free
    );

    if (!tcp)
        co_return std::unexpected(std::error_code(errno, std::generic_category()));

    CO_EXPECT(uv::expected([&] {
        return uv_tcp_init(getEventLoop()->raw(), tcp.get());
    }));

    uv::Handle handle(
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(tcp.release()),
            std::free
        }
    );

    CO_EXPECT(co_await mListener.accept(handle.raw()));
    co_return TCPStream{Stream{std::move(handle)}};
}

#ifdef _WIN32
asyncio::net::NamedPipeStream::NamedPipeStream(Pipe pipe) : mPipe(std::move(pipe)) {
}

asyncio::task::Task<asyncio::net::NamedPipeStream, std::error_code>
asyncio::net::NamedPipeStream::connect(const std::string name) {
    std::unique_ptr<uv_pipe_t, decltype(&std::free)> pipe(
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    );

    if (!pipe)
        co_return std::unexpected(std::error_code(errno, std::generic_category()));

    CO_EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    Pipe stream(
        uv::Handle{
            std::unique_ptr<uv_stream_t, decltype(&std::free)>{
                reinterpret_cast<uv_stream_t *>(pipe.release()),
                std::free
            }
        }
    );

    Promise<void, std::error_code> promise;
    uv_connect_t request = {.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_pipe_connect2(
            &request,
            reinterpret_cast<uv_pipe_t *>(stream.handle().raw()),
            name.c_str(),
            name.length(),
            UV_PIPE_NO_TRUNCATE,
            // ReSharper disable once CppParameterMayBeConstPtrOrRef
            [](uv_connect_t *req, const int status) {
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
    co_return NamedPipeStream{std::move(stream)};
}

asyncio::FileDescriptor asyncio::net::NamedPipeStream::fd() const {
    return mPipe.fd();
}

std::expected<DWORD, std::error_code> asyncio::net::NamedPipeStream::clientProcessID() const {
    DWORD pid;

    EXPECT(zero::os::nt::expected([&] {
        return GetNamedPipeClientProcessId(mPipe.fd(), &pid);
    }));

    return pid;
}

std::expected<DWORD, std::error_code> asyncio::net::NamedPipeStream::serverProcessID() const {
    DWORD pid;

    EXPECT(zero::os::nt::expected([&] {
        return GetNamedPipeServerProcessId(mPipe.fd(), &pid);
    }));

    return pid;
}

std::expected<void, std::error_code> asyncio::net::NamedPipeStream::chmod(const int mode) {
    return mPipe.chmod(mode);
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::NamedPipeStream::read(const std::span<std::byte> data) {
    co_return co_await mPipe.read(data);
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::NamedPipeStream::write(const std::span<const std::byte> data) {
    co_return co_await mPipe.write(data);
}

asyncio::task::Task<void, std::error_code> asyncio::net::NamedPipeStream::close() {
    mPipe.handle().close();
    co_return {};
}

asyncio::net::NamedPipeListener::NamedPipeListener(Listener listener) : mListener(std::move(listener)) {
}

std::expected<asyncio::net::NamedPipeListener, std::error_code>
asyncio::net::NamedPipeListener::listen(const std::string &name) {
    std::unique_ptr<uv_pipe_t, decltype(&std::free)> pipe(
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    );

    if (!pipe)
        return std::unexpected(std::error_code(errno, std::generic_category()));

    EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    uv::Handle handle(
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(pipe.release()),
            std::free
        }
    );

    EXPECT(uv::expected([&] {
        return uv_pipe_bind2(
            reinterpret_cast<uv_pipe_t *>(handle.raw()),
            name.c_str(),
            name.length(),
            UV_PIPE_NO_TRUNCATE
        );
    }));

    auto listener = Listener::make(std::move(handle));
    EXPECT(listener);

    return NamedPipeListener{*std::move(listener)};
}

asyncio::task::Task<asyncio::net::NamedPipeStream, std::error_code> asyncio::net::NamedPipeListener::accept() {
    std::unique_ptr<uv_pipe_t, decltype(&std::free)> pipe(
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    );

    if (!pipe)
        co_return std::unexpected(std::error_code(errno, std::generic_category()));

    CO_EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    uv::Handle handle(
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(pipe.release()),
            std::free
        }
    );

    CO_EXPECT(co_await mListener.accept(handle.raw()));
    co_return NamedPipeStream{Pipe{std::move(handle)}};
}
#else
asyncio::net::UnixStream::UnixStream(Pipe pipe) : mPipe(std::move(pipe)) {
}

std::expected<asyncio::net::UnixStream, std::error_code> asyncio::net::UnixStream::from(const uv_os_sock_t socket) {
    auto pipe = Pipe::from(socket);
    EXPECT(pipe);
    return UnixStream{*std::move(pipe)};
}

asyncio::task::Task<asyncio::net::UnixStream, std::error_code>
asyncio::net::UnixStream::connect(std::string path) {
    assert(!path.empty());

    if (path.front() == '@')
        path.front() = '\0';

    std::unique_ptr<uv_pipe_t, decltype(&std::free)> pipe(
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    );

    if (!pipe)
        co_return std::unexpected(std::error_code(errno, std::generic_category()));

    CO_EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    Pipe stream(uv::Handle{std::unique_ptr<uv_stream_t>{reinterpret_cast<uv_stream_t *>(pipe.release())}});

    Promise<void, std::error_code> promise;
    uv_connect_t request = {.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_pipe_connect2(
            &request,
            reinterpret_cast<uv_pipe_t *>(stream.handle().raw()),
            path.c_str(),
            path.length(),
            UV_PIPE_NO_TRUNCATE,
            // ReSharper disable once CppParameterMayBeConstPtrOrRef
            [](uv_connect_t *req, const int status) {
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
    co_return UnixStream{std::move(stream)};
}

std::expected<asyncio::net::UnixListener, std::error_code>
asyncio::net::UnixListener::listen(const UnixAddress &address) {
    return listen(address.path);
}

asyncio::FileDescriptor asyncio::net::UnixStream::fd() const {
    return mPipe.fd();
}

std::expected<asyncio::net::Address, std::error_code> asyncio::net::UnixStream::localAddress() const {
    auto address = mPipe.localAddress();
    EXPECT(address);

    if (!address->empty() && address->front() == '\0')
        address->front() = '@';

    return UnixAddress{*std::move(address)};
}

std::expected<asyncio::net::Address, std::error_code> asyncio::net::UnixStream::remoteAddress() const {
    auto address = mPipe.remoteAddress();
    EXPECT(address);

    if (!address->empty() && address->front() == '\0')
        address->front() = '@';

    return UnixAddress{*std::move(address)};
}

std::expected<asyncio::net::UnixStream::Credential, std::error_code> asyncio::net::UnixStream::peerCredential() const {
#ifdef __linux__
    ucred cred = {};
    socklen_t length = sizeof(cred);

    EXPECT(zero::os::unix::expected([&] {
        return getsockopt(mPipe.fd(), SOL_SOCKET, SO_PEERCRED, &cred, &length);
    }));

    if (length != sizeof(cred))
        return std::unexpected(std::error_code(errno, std::system_category()));

    return Credential{cred.uid, cred.gid, cred.pid};
#elif __APPLE__
    Credential credential;
    const auto fd = mPipe.fd();

    EXPECT(zero::os::unix::expected([&] {
        return getpeereid(fd, &credential.uid, &credential.gid);
    }));

    pid_t pid;
    socklen_t length = sizeof(pid);

    EXPECT(zero::os::unix::expected([&] {
        return getsockopt(fd, SOL_LOCAL, LOCAL_PEERPID, &pid, &length);
    }));

    if (length != sizeof(pid))
        return std::unexpected(std::error_code(errno, std::system_category()));

    credential.pid = pid;
    return credential;
#else
#error "unsupported platform"
#endif
}

std::expected<void, std::error_code> asyncio::net::UnixStream::chmod(const int mode) {
    return mPipe.chmod(mode);
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::UnixStream::read(const std::span<std::byte> data) {
    co_return co_await mPipe.read(data);
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::UnixStream::write(const std::span<const std::byte> data) {
    co_return co_await mPipe.write(data);
}

asyncio::task::Task<std::pair<std::size_t, asyncio::net::Address>, std::error_code>
asyncio::net::UnixStream::readFrom(const std::span<std::byte> data) {
    auto remote = remoteAddress();
    CO_EXPECT(remote);

    auto n = co_await read(data);
    CO_EXPECT(n);

    co_return std::pair{*n, *std::move(remote)};
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::UnixStream::writeTo(const std::span<const std::byte> data, const Address) {
    co_return co_await write(data);
}

asyncio::task::Task<void, std::error_code> asyncio::net::UnixStream::close() {
    mPipe.handle().close();
    co_return {};
}

asyncio::net::UnixListener::UnixListener(Listener listener) : mListener(std::move(listener)) {
}

std::expected<asyncio::net::UnixListener, std::error_code> asyncio::net::UnixListener::listen(std::string path) {
    assert(!path.empty());

    if (path.front() == '@')
        path.front() = '\0';

    std::unique_ptr<uv_pipe_t, decltype(&std::free)> pipe(
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    );

    if (!pipe)
        return std::unexpected(std::error_code(errno, std::generic_category()));

    EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    uv::Handle handle(
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(pipe.release()),
            std::free
        }
    );

    EXPECT(uv::expected([&] {
        return uv_pipe_bind2(
            reinterpret_cast<uv_pipe_t *>(handle.raw()),
            path.c_str(),
            path.length(),
            UV_PIPE_NO_TRUNCATE
        );
    }));

    auto listener = Listener::make(std::move(handle));
    EXPECT(listener);

    return UnixListener{*std::move(listener)};
}

asyncio::task::Task<asyncio::net::UnixStream, std::error_code> asyncio::net::UnixListener::accept() {
    std::unique_ptr<uv_pipe_t, decltype(&std::free)> pipe(
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    );

    if (!pipe)
        co_return std::unexpected(std::error_code(errno, std::generic_category()));

    CO_EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    uv::Handle handle(
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(pipe.release()),
            std::free
        }
    );

    CO_EXPECT(co_await mListener.accept(handle.raw()));
    co_return UnixStream{Pipe{std::move(handle)}};
}
#endif

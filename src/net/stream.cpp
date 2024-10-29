#include <asyncio/net/stream.h>
#include <asyncio/net/dns.h>

#ifdef _WIN32
#include <zero/os/windows/error.h>
#elif defined(__linux__)
#include <zero/os/unix/error.h>
#elif defined(__APPLE__)
#include <unistd.h>
#include <sys/un.h>
#include <zero/os/unix/error.h>
#endif

asyncio::net::TCPStream::TCPStream(Stream stream) : mStream{std::move(stream)} {
}

std::expected<asyncio::net::TCPStream, std::error_code> asyncio::net::TCPStream::make() {
    std::unique_ptr<uv_tcp_t, decltype(&std::free)> tcp{
        static_cast<uv_tcp_t *>(std::malloc(sizeof(uv_tcp_t))),
        std::free
    };

    if (!tcp)
        return std::unexpected{std::error_code{errno, std::generic_category()}};

    EXPECT(uv::expected([&] {
        return uv_tcp_init(getEventLoop()->raw(), tcp.get());
    }));

    return TCPStream{
        Stream{
            uv::Handle{
                std::unique_ptr<uv_stream_t, decltype(&std::free)>{
                    reinterpret_cast<uv_stream_t *>(tcp.release()),
                    std::free
                }
            }
        }
    };
}

// TODO
// except for MSVC, adding const will fail to compile.
// ReSharper disable once CppParameterMayBeConst
asyncio::task::Task<asyncio::net::TCPStream, std::error_code>
asyncio::net::TCPStream::connect(SocketAddress address) {
    auto tcp = make();
    CO_EXPECT(tcp);

    Promise<void, std::error_code> promise;
    uv_connect_t request{.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_tcp_connect(
            &request,
            reinterpret_cast<uv_tcp_t *>(tcp->mStream.mStream.raw()),
            address.first.get(),
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
    co_return *std::move(tcp);
}

std::expected<asyncio::net::TCPStream, std::error_code> asyncio::net::TCPStream::from(const uv_os_sock_t socket) {
    auto tcp = make();
    EXPECT(tcp);

    EXPECT(uv::expected([&] {
        return uv_tcp_open(reinterpret_cast<uv_tcp_t *>(tcp->mStream.mStream.raw()), socket);
    }));

    return *std::move(tcp);
}

asyncio::task::Task<asyncio::net::TCPStream, std::error_code>
asyncio::net::TCPStream::connect(const std::string host, const std::uint16_t port) {
    const auto addresses = co_await dns::getAddressInfo(
        host,
        std::to_string(port),
        addrinfo{
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM
        }
    );
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
            co_return std::unexpected{result.error()};
    }
}

asyncio::task::Task<asyncio::net::TCPStream, std::error_code>
asyncio::net::TCPStream::connect(const IPAddress address) {
    auto socketAddress = std::visit(
        [](const auto &arg) {
            return socketAddressFrom(arg);
        },
        address
    );
    CO_EXPECT(socketAddress);
    co_return co_await connect(*std::move(socketAddress));
}

asyncio::FileDescriptor asyncio::net::TCPStream::fd() const {
    const auto fd = mStream.mStream.fd();
    assert(fd);
    return *fd;
}

std::expected<asyncio::net::Address, std::error_code> asyncio::net::TCPStream::localAddress() const {
    sockaddr_storage storage{};
    auto length = static_cast<int>(sizeof(sockaddr_storage));

    EXPECT(uv::expected([&] {
        return uv_tcp_getsockname(
            reinterpret_cast<const uv_tcp_t *>(mStream.mStream.raw()),
            reinterpret_cast<sockaddr *>(&storage),
            &length
        );
    }));

    return addressFrom(reinterpret_cast<const sockaddr *>(&storage), length);
}

std::expected<asyncio::net::Address, std::error_code> asyncio::net::TCPStream::remoteAddress() const {
    sockaddr_storage storage{};
    auto length = static_cast<int>(sizeof(sockaddr_storage));

    EXPECT(uv::expected([&] {
        return uv_tcp_getpeername(
            reinterpret_cast<const uv_tcp_t *>(mStream.mStream.raw()),
            reinterpret_cast<sockaddr *>(&storage),
            &length
        );
    }));

    return addressFrom(reinterpret_cast<const sockaddr *>(&storage), length);
}

std::expected<void, std::error_code> asyncio::net::TCPStream::noDelay(const bool enable) {
    EXPECT(uv::expected([&] {
        return uv_tcp_nodelay(reinterpret_cast<uv_tcp_t *>(mStream.mStream.raw()), enable);
    }));
    return {};
}

std::expected<void, std::error_code>
asyncio::net::TCPStream::keepalive(const bool enable, const std::optional<std::chrono::seconds> delay) {
    using namespace std::chrono_literals;
    EXPECT(uv::expected([&] {
        return uv_tcp_keepalive(
            reinterpret_cast<uv_tcp_t *>(mStream.mStream.raw()),
            enable,
            delay.value_or(0s).count()
        );
    }));
    return {};
}

std::expected<void, std::error_code> asyncio::net::TCPStream::simultaneousAccepts(const bool enable) {
    EXPECT(uv::expected([&] {
        return uv_tcp_simultaneous_accepts(reinterpret_cast<uv_tcp_t *>(mStream.mStream.raw()), enable);
    }));
    return {};
}

asyncio::task::Task<void, std::error_code> asyncio::net::TCPStream::shutdown() {
    co_return co_await mStream.shutdown();
}

asyncio::task::Task<void, std::error_code> asyncio::net::TCPStream::closeReset() {
    const auto handle = mStream.mStream.release();

    Promise<void> promise;
    handle->data = &promise;

    uv_tcp_close_reset(
        reinterpret_cast<uv_tcp_t *>(handle.get()),
        [](auto *h) {
            static_cast<Promise<void> *>(h->data)->resolve();
        }
    );

    co_await promise.getFuture();
    co_return {};
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

    const auto n = co_await read(data);
    CO_EXPECT(n);

    co_return std::pair{*n, *std::move(remote)};
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::TCPStream::writeTo(const std::span<const std::byte> data, const Address) {
    co_return co_await write(data);
}

asyncio::task::Task<void, std::error_code> asyncio::net::TCPStream::close() {
    co_return co_await mStream.close();
}

asyncio::net::TCPListener::TCPListener(Listener listener) : mListener{std::move(listener)} {
}

std::expected<asyncio::net::TCPListener, std::error_code>
asyncio::net::TCPListener::listen(const SocketAddress &address) {
    std::unique_ptr<uv_tcp_t, decltype(&std::free)> tcp{
        static_cast<uv_tcp_t *>(std::malloc(sizeof(uv_tcp_t))),
        std::free
    };

    if (!tcp)
        return std::unexpected{std::error_code{errno, std::generic_category()}};

    EXPECT(uv::expected([&] {
        return uv_tcp_init(getEventLoop()->raw(), tcp.get());
    }));

    uv::Handle handle{
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(tcp.release()),
            std::free
        }
    };

    EXPECT(uv::expected([&] {
        return uv_tcp_bind(reinterpret_cast<uv_tcp_t *>(handle.raw()), address.first.get(), 0);
    }));

    auto listener = Listener::make(std::move(handle));
    EXPECT(listener);

    return TCPListener{*std::move(listener)};
}

std::expected<asyncio::net::TCPListener, std::error_code>
asyncio::net::TCPListener::listen(const std::string &ip, const std::uint16_t port) {
    const auto address = ipAddressFrom(ip, port);
    EXPECT(address);

    auto socketAddress = std::visit(
        [](const auto &arg) {
            return socketAddressFrom(arg);
        },
        *address
    );
    EXPECT(socketAddress);

    return listen(*std::move(socketAddress));
}

std::expected<asyncio::net::TCPListener, std::error_code>
asyncio::net::TCPListener::listen(const IPAddress &address) {
    auto socketAddress = std::visit(
        [](const auto &arg) {
            return socketAddressFrom(arg);
        },
        address
    );
    EXPECT(socketAddress);
    return listen(*std::move(socketAddress));
}

asyncio::FileDescriptor asyncio::net::TCPListener::fd() const {
    const auto fd = mListener.mCore->stream.fd();
    assert(fd);
    return *fd;
}

std::expected<asyncio::net::IPAddress, std::error_code> asyncio::net::TCPListener::address() const {
    sockaddr_storage storage{};
    auto length = static_cast<int>(sizeof(sockaddr_storage));

    EXPECT(uv::expected([&] {
        return uv_tcp_getsockname(
            reinterpret_cast<const uv_tcp_t *>(mListener.mCore->stream.raw()),
            reinterpret_cast<sockaddr *>(&storage),
            &length
        );
    }));

    auto address = addressFrom(reinterpret_cast<const sockaddr *>(&storage), length);
    EXPECT(address);

    return std::visit(
        []<typename T>(T &&arg) -> IPAddress {
            if constexpr (!std::is_same_v<std::remove_cvref_t<T>, UnixAddress>)
                return std::forward<T>(arg);
            else
                std::abort();
        },
        *std::move(address)
    );
}

asyncio::task::Task<asyncio::net::TCPStream, std::error_code>
asyncio::net::TCPListener::accept() {
    std::unique_ptr<uv_tcp_t, decltype(&std::free)> tcp{
        static_cast<uv_tcp_t *>(std::malloc(sizeof(uv_tcp_t))),
        std::free
    };

    if (!tcp)
        co_return std::unexpected{std::error_code{errno, std::generic_category()}};

    CO_EXPECT(uv::expected([&] {
        return uv_tcp_init(getEventLoop()->raw(), tcp.get());
    }));

    uv::Handle handle{
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(tcp.release()),
            std::free
        }
    };

    CO_EXPECT(co_await mListener.accept(handle.raw()));
    co_return TCPStream{Stream{std::move(handle)}};
}

asyncio::task::Task<void, std::error_code> asyncio::net::TCPListener::close() {
    co_return co_await mListener.close();
}

#ifdef _WIN32
asyncio::net::NamedPipeStream::NamedPipeStream(Pipe pipe) : mPipe{std::move(pipe)} {
}

std::expected<asyncio::net::NamedPipeStream, std::error_code> asyncio::net::NamedPipeStream::from(const int fd) {
    auto pipe = Pipe::from(fd);
    EXPECT(pipe);
    return NamedPipeStream{*std::move(pipe)};
}

asyncio::task::Task<asyncio::net::NamedPipeStream, std::error_code>
asyncio::net::NamedPipeStream::connect(const std::string name) {
    std::unique_ptr<uv_pipe_t, decltype(&std::free)> pipe{
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    };

    if (!pipe)
        co_return std::unexpected{std::error_code{errno, std::generic_category()}};

    CO_EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    Pipe stream{
        uv::Handle{
            std::unique_ptr<uv_stream_t, decltype(&std::free)>{
                reinterpret_cast<uv_stream_t *>(pipe.release()),
                std::free
            }
        }
    };

    Promise<void, std::error_code> promise;
    uv_connect_t request{.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_pipe_connect2(
            &request,
            reinterpret_cast<uv_pipe_t *>(stream.mStream.raw()),
            name.c_str(),
            name.length(),
            UV_PIPE_NO_TRUNCATE,
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
    co_return NamedPipeStream{std::move(stream)};
}

asyncio::FileDescriptor asyncio::net::NamedPipeStream::fd() const {
    return mPipe.fd();
}

std::expected<DWORD, std::error_code> asyncio::net::NamedPipeStream::clientProcessID() const {
    DWORD pid{};

    EXPECT(zero::os::windows::expected([&] {
        return GetNamedPipeClientProcessId(mPipe.fd(), &pid);
    }));

    return pid;
}

std::expected<DWORD, std::error_code> asyncio::net::NamedPipeStream::serverProcessID() const {
    DWORD pid{};

    EXPECT(zero::os::windows::expected([&] {
        return GetNamedPipeServerProcessId(mPipe.fd(), &pid);
    }));

    return pid;
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
    co_return co_await mPipe.close();
}

asyncio::net::NamedPipeListener::NamedPipeListener(PipeListener listener) : mListener{std::move(listener)} {
}

std::expected<asyncio::net::NamedPipeListener, std::error_code>
asyncio::net::NamedPipeListener::listen(const std::string &name) {
    std::unique_ptr<uv_pipe_t, decltype(&std::free)> pipe{
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    };

    if (!pipe)
        return std::unexpected{std::error_code{errno, std::generic_category()}};

    EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    uv::Handle handle{
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(pipe.release()),
            std::free
        }
    };

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

    return NamedPipeListener{PipeListener{*std::move(listener)}};
}

asyncio::FileDescriptor asyncio::net::NamedPipeListener::fd() const {
    return mListener.fd();
}

std::expected<std::string, std::error_code> asyncio::net::NamedPipeListener::address() const {
    return mListener.address();
}

std::expected<void, std::error_code> asyncio::net::NamedPipeListener::chmod(const int mode) {
    return mListener.chmod(mode);
}

asyncio::task::Task<asyncio::net::NamedPipeStream, std::error_code> asyncio::net::NamedPipeListener::accept() {
    std::unique_ptr<uv_pipe_t, decltype(&std::free)> pipe{
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    };

    if (!pipe)
        co_return std::unexpected{std::error_code{errno, std::generic_category()}};

    CO_EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    uv::Handle handle{
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(pipe.release()),
            std::free
        }
    };

    CO_EXPECT(co_await mListener.accept(handle.raw()));
    co_return NamedPipeStream{Pipe{std::move(handle)}};
}

asyncio::task::Task<void, std::error_code> asyncio::net::NamedPipeListener::close() {
    co_return co_await mListener.close();
}
#else
asyncio::net::UnixStream::UnixStream(Pipe pipe) : mPipe{std::move(pipe)} {
}

std::expected<asyncio::net::UnixStream, std::error_code> asyncio::net::UnixStream::from(const int socket) {
    auto pipe = Pipe::from(socket);
    EXPECT(pipe);
    return UnixStream{*std::move(pipe)};
}

asyncio::task::Task<asyncio::net::UnixStream, std::error_code>
asyncio::net::UnixStream::connect(std::string path) {
    assert(!path.empty());

    if (path.front() == '@')
        path.front() = '\0';

    std::unique_ptr<uv_pipe_t, decltype(&std::free)> pipe{
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    };

    if (!pipe)
        co_return std::unexpected{std::error_code{errno, std::generic_category()}};

    CO_EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    Pipe stream{
        uv::Handle{
            std::unique_ptr<uv_stream_t, decltype(&std::free)>{
                reinterpret_cast<uv_stream_t *>(pipe.release()),
                std::free
            }
        }
    };

    Promise<void, std::error_code> promise;
    uv_connect_t request{.data = &promise};

    CO_EXPECT(uv::expected([&] {
        return uv_pipe_connect2(
            &request,
            reinterpret_cast<uv_pipe_t *>(stream.mStream.raw()),
            path.c_str(),
            path.length(),
            UV_PIPE_NO_TRUNCATE,
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
    co_return UnixStream{std::move(stream)};
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
    ucred cred{};
    auto length = static_cast<socklen_t>(sizeof(cred));

    EXPECT(zero::os::unix::expected([&] {
        return getsockopt(mPipe.fd(), SOL_SOCKET, SO_PEERCRED, &cred, &length);
    }));

    if (length != sizeof(cred))
        return std::unexpected{std::error_code{errno, std::system_category()}};

    return Credential{cred.uid, cred.gid, cred.pid};
#elif defined(__APPLE__)
    Credential credential;
    const auto fd = mPipe.fd();

    EXPECT(zero::os::unix::expected([&] {
        return getpeereid(fd, &credential.uid, &credential.gid);
    }));

    pid_t pid{};
    auto length = static_cast<socklen_t>(sizeof(pid));

    EXPECT(zero::os::unix::expected([&] {
        return getsockopt(fd, SOL_LOCAL, LOCAL_PEERPID, &pid, &length);
    }));

    if (length != sizeof(pid))
        return std::unexpected{std::error_code{errno, std::system_category()}};

    credential.pid = pid;
    return credential;
#else
#error "unsupported platform"
#endif
}

asyncio::task::Task<void, std::error_code> asyncio::net::UnixStream::shutdown() {
    co_return co_await mPipe.shutdown();
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

    const auto n = co_await read(data);
    CO_EXPECT(n);

    co_return std::pair{*n, *std::move(remote)};
}

asyncio::task::Task<std::size_t, std::error_code>
asyncio::net::UnixStream::writeTo(const std::span<const std::byte> data, const Address) {
    co_return co_await write(data);
}

asyncio::task::Task<void, std::error_code> asyncio::net::UnixStream::close() {
    co_return co_await mPipe.close();
}

asyncio::net::UnixListener::UnixListener(PipeListener listener) : mListener{std::move(listener)} {
}

std::expected<asyncio::net::UnixListener, std::error_code> asyncio::net::UnixListener::listen(std::string path) {
    assert(!path.empty());

    if (path.front() == '@')
        path.front() = '\0';

    std::unique_ptr<uv_pipe_t, decltype(&std::free)> pipe{
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    };

    if (!pipe)
        return std::unexpected{std::error_code{errno, std::generic_category()}};

    EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    uv::Handle handle{
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(pipe.release()),
            std::free
        }
    };

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

    return UnixListener{PipeListener{*std::move(listener)}};
}

std::expected<asyncio::net::UnixListener, std::error_code>
asyncio::net::UnixListener::listen(const UnixAddress &address) {
    return listen(address.path);
}

asyncio::FileDescriptor asyncio::net::UnixListener::fd() const {
    const auto fd = mListener.mCore->stream.fd();
    assert(fd);
    return *fd;
}

std::expected<std::string, std::error_code> asyncio::net::UnixListener::address() const {
    return mListener.address();
}

std::expected<void, std::error_code> asyncio::net::UnixListener::chmod(const int mode) {
    return mListener.chmod(mode);
}

asyncio::task::Task<asyncio::net::UnixStream, std::error_code> asyncio::net::UnixListener::accept() {
    std::unique_ptr<uv_pipe_t, decltype(&std::free)> pipe{
        static_cast<uv_pipe_t *>(std::malloc(sizeof(uv_pipe_t))),
        std::free
    };

    if (!pipe)
        co_return std::unexpected{std::error_code{errno, std::generic_category()}};

    CO_EXPECT(uv::expected([&] {
        return uv_pipe_init(getEventLoop()->raw(), pipe.get(), 0);
    }));

    uv::Handle handle{
        std::unique_ptr<uv_stream_t, decltype(&std::free)>{
            reinterpret_cast<uv_stream_t *>(pipe.release()),
            std::free
        }
    };

    CO_EXPECT(co_await mListener.accept(handle.raw()));
    co_return UnixStream{Pipe{std::move(handle)}};
}

asyncio::task::Task<void, std::error_code> asyncio::net::UnixListener::close() {
    co_return co_await mListener.close();
}
#endif

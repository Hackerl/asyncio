#ifndef ASYNCIO_NET_STREAM_H
#define ASYNCIO_NET_STREAM_H

#include "net.h"
#include <asyncio/pipe.h>

namespace asyncio::net {
    class TCPStream final : public ISocket, public ICloseable {
    public:
        explicit TCPStream(Stream stream);

    private:
        static zero::async::coroutine::Task<TCPStream, std::error_code> connect(SocketAddress address);

    public:
        static zero::async::coroutine::Task<TCPStream, std::error_code> connect(std::string host, unsigned short port);
        static zero::async::coroutine::Task<TCPStream, std::error_code> connect(IPv4Address address);
        static zero::async::coroutine::Task<TCPStream, std::error_code> connect(IPv6Address address);

        [[nodiscard]] std::expected<Address, std::error_code> localAddress() const override;
        [[nodiscard]] std::expected<Address, std::error_code> remoteAddress() const override;

        zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        zero::async::coroutine::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        zero::async::coroutine::Task<std::pair<std::size_t, Address>, std::error_code>
        readFrom(std::span<std::byte> data) override;

        zero::async::coroutine::Task<std::size_t, std::error_code>
        writeTo(std::span<const std::byte> data, Address address) override;

        zero::async::coroutine::Task<void, std::error_code> close() override;

    private:
        Stream mStream;
    };

    class TCPListener {
    public:
        explicit TCPListener(Listener listener);

    private:
        static std::expected<TCPListener, std::error_code> listen(const SocketAddress &address);

    public:
        static std::expected<TCPListener, std::error_code> listen(const std::string &ip, unsigned short port);
        static std::expected<TCPListener, std::error_code> listen(const IPv4Address &address);
        static std::expected<TCPListener, std::error_code> listen(const IPv6Address &address);

        zero::async::coroutine::Task<TCPStream, std::error_code> accept();

    private:
        Listener mListener;
    };

#ifdef _WIN32
    class NamedPipeStream final : public IReader, public IWriter, public ICloseable {
    public:
        explicit NamedPipeStream(Pipe pipe);
        static zero::async::coroutine::Task<NamedPipeStream, std::error_code> connect(std::string name);

        zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        zero::async::coroutine::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        zero::async::coroutine::Task<void, std::error_code> close() override;

    private:
        Pipe mPipe;
    };

    class NamedPipeListener {
    public:
        explicit NamedPipeListener(Listener listener);
        static std::expected<NamedPipeListener, std::error_code> listen(const std::string &name);

        zero::async::coroutine::Task<NamedPipeStream, std::error_code> accept();

    private:
        Listener mListener;
    };
#else
    class UnixStream final : public ISocket, public ICloseable {
    public:
        explicit UnixStream(Pipe pipe);
        static zero::async::coroutine::Task<UnixStream, std::error_code> connect(std::string path);

        [[nodiscard]] std::expected<Address, std::error_code> localAddress() const override;
        [[nodiscard]] std::expected<Address, std::error_code> remoteAddress() const override;

        zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        zero::async::coroutine::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        zero::async::coroutine::Task<std::pair<std::size_t, Address>, std::error_code>
        readFrom(std::span<std::byte> data) override;

        zero::async::coroutine::Task<std::size_t, std::error_code>
        writeTo(std::span<const std::byte> data, Address address) override;

        zero::async::coroutine::Task<void, std::error_code> close() override;

    private:
        Pipe mPipe;
    };

    class UnixListener {
    public:
        explicit UnixListener(Listener listener);

        static std::expected<UnixListener, std::error_code> listen(std::string path);
        static std::expected<UnixListener, std::error_code> listen(const UnixAddress &address);

        zero::async::coroutine::Task<UnixStream, std::error_code> accept();

    private:
        Listener mListener;
    };
#endif
}

#endif //ASYNCIO_NET_STREAM_H

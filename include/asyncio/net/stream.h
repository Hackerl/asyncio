#ifndef ASYNCIO_NET_STREAM_H
#define ASYNCIO_NET_STREAM_H

#include "net.h"
#include <asyncio/stream.h>

namespace asyncio::net {
    class TCPStream : public Stream, public ISocket {
    public:
        explicit TCPStream(uv::Handle<uv_stream_t> stream);

    private:
        static zero::async::coroutine::Task<TCPStream, std::error_code> connect(SocketAddress address);

    public:
        static zero::async::coroutine::Task<TCPStream, std::error_code> connect(std::string host, unsigned short port);
        static zero::async::coroutine::Task<TCPStream, std::error_code> connect(IPv4Address address);
        static zero::async::coroutine::Task<TCPStream, std::error_code> connect(IPv6Address address);

        [[nodiscard]] std::expected<Address, std::error_code> localAddress() const override;
        [[nodiscard]] std::expected<Address, std::error_code> remoteAddress() const override;

        zero::async::coroutine::Task<std::pair<std::size_t, Address>, std::error_code>
        readFrom(std::span<std::byte> data) override;

        zero::async::coroutine::Task<std::size_t, std::error_code>
        writeTo(std::span<const std::byte> data, Address address) override;

        zero::async::coroutine::Task<void, std::error_code> close() override;
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
}

#endif //ASYNCIO_NET_STREAM_H

#ifndef ASYNCIO_NET_STREAM_H
#define ASYNCIO_NET_STREAM_H

#include "net.h"
#include <asyncio/pipe.h>

namespace asyncio::net {
    class TCPStream final : public ISocket, public IHalfCloseable {
    public:
        explicit TCPStream(Stream stream);

    private:
        static TCPStream make();
        static task::Task<TCPStream, std::error_code> connect(SocketAddress address);

    public:
        static std::expected<TCPStream, std::error_code> from(uv_os_sock_t socket);

        static task::Task<TCPStream, std::error_code> connect(std::string host, std::uint16_t port);
        static task::Task<TCPStream, std::error_code> connect(IPAddress address);

        [[nodiscard]] FileDescriptor fd() const override;

        [[nodiscard]] std::expected<Address, std::error_code> localAddress() const override;
        [[nodiscard]] std::expected<Address, std::error_code> remoteAddress() const override;

        std::expected<void, std::error_code> noDelay(bool enable);

        std::expected<void, std::error_code>
        keepalive(bool enable, std::optional<std::chrono::seconds> delay = std::nullopt);

        std::expected<void, std::error_code> simultaneousAccepts(bool enable);

        task::Task<void, std::error_code> shutdown() override;
        task::Task<void, std::error_code> closeReset();

        task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        task::Task<std::pair<std::size_t, Address>, std::error_code>
        readFrom(std::span<std::byte> data) override;

        task::Task<std::size_t, std::error_code>
        writeTo(std::span<const std::byte> data, Address address) override;

        task::Task<void, std::error_code> close() override;

    private:
        Stream mStream;
    };

    class TCPListener final : public IFileDescriptor, public ICloseable {
    public:
        explicit TCPListener(Listener listener);

    private:
        static std::expected<TCPListener, std::error_code> listen(const SocketAddress &address);

    public:
        static std::expected<TCPListener, std::error_code> listen(const std::string &ip, std::uint16_t port);
        static std::expected<TCPListener, std::error_code> listen(const IPAddress &address);

        [[nodiscard]] FileDescriptor fd() const override;
        [[nodiscard]] std::expected<IPAddress, std::error_code> address() const;

        task::Task<TCPStream, std::error_code> accept();
        task::Task<void, std::error_code> close() override;

    private:
        Listener mListener;
    };

#ifdef _WIN32
    class NamedPipeStream final : public IFileDescriptor, public IReader, public IWriter, public ICloseable {
    public:
        explicit NamedPipeStream(Pipe pipe);

        static std::expected<NamedPipeStream, std::error_code> from(int fd);
        static task::Task<NamedPipeStream, std::error_code> connect(std::string name);

        [[nodiscard]] FileDescriptor fd() const override;

        [[nodiscard]] std::expected<DWORD, std::error_code> clientProcessID() const;
        [[nodiscard]] std::expected<DWORD, std::error_code> serverProcessID() const;

        task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        task::Task<void, std::error_code> close() override;

    private:
        Pipe mPipe;
    };

    class NamedPipeListener final : public IFileDescriptor, public ICloseable {
    public:
        explicit NamedPipeListener(PipeListener listener);
        static std::expected<NamedPipeListener, std::error_code> listen(const std::string &name);

        [[nodiscard]] FileDescriptor fd() const override;
        [[nodiscard]] std::expected<std::string, std::error_code> address() const;

        std::expected<void, std::error_code> chmod(int mode);

        task::Task<NamedPipeStream, std::error_code> accept();
        task::Task<void, std::error_code> close() override;

    private:
        PipeListener mListener;
    };
#else
    class UnixStream final : public ISocket, public IHalfCloseable {
    public:
        struct Credential {
            uid_t uid{};
            gid_t gid{};
            std::optional<pid_t> pid;
        };

        explicit UnixStream(Pipe pipe);

        static std::expected<UnixStream, std::error_code> from(int socket);
        static task::Task<UnixStream, std::error_code> connect(std::string path);

        [[nodiscard]] FileDescriptor fd() const override;

        [[nodiscard]] std::expected<Address, std::error_code> localAddress() const override;
        [[nodiscard]] std::expected<Address, std::error_code> remoteAddress() const override;

        [[nodiscard]] std::expected<Credential, std::error_code> peerCredential() const;

        task::Task<void, std::error_code> shutdown() override;

        task::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        task::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        task::Task<std::pair<std::size_t, Address>, std::error_code>
        readFrom(std::span<std::byte> data) override;

        task::Task<std::size_t, std::error_code>
        writeTo(std::span<const std::byte> data, Address address) override;

        task::Task<void, std::error_code> close() override;

    private:
        Pipe mPipe;
    };

    class UnixListener final : public IFileDescriptor, public ICloseable {
    public:
        explicit UnixListener(PipeListener listener);

        static std::expected<UnixListener, std::error_code> listen(std::string path);
        static std::expected<UnixListener, std::error_code> listen(const UnixAddress &address);

        [[nodiscard]] FileDescriptor fd() const override;
        [[nodiscard]] std::expected<std::string, std::error_code> address() const;

        std::expected<void, std::error_code> chmod(int mode);

        task::Task<UnixStream, std::error_code> accept();
        task::Task<void, std::error_code> close() override;

    private:
        PipeListener mListener;
    };
#endif
}

#endif //ASYNCIO_NET_STREAM_H

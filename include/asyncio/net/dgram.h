#ifndef ASYNCIO_DGRAM_H
#define ASYNCIO_DGRAM_H

#include "net.h"
#include <asyncio/ev/event.h>

namespace asyncio::net::dgram {
    class Socket final : public ISocket, public Reader, public Writer {
    public:
        Socket(FileDescriptor fd, std::array<ev::Event, 2> events);
        Socket(Socket &&rhs) noexcept;
        ~Socket() override;

        zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;
        zero::async::coroutine::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;
        zero::async::coroutine::Task<void, std::error_code> close() override;

        zero::async::coroutine::Task<std::pair<std::size_t, Address>, std::error_code>
        readFrom(std::span<std::byte> data) override;

        zero::async::coroutine::Task<std::size_t, std::error_code>
        writeTo(std::span<const std::byte> data, Address address) override;

        [[nodiscard]] tl::expected<Address, std::error_code> localAddress() const override;
        [[nodiscard]] tl::expected<Address, std::error_code> remoteAddress() const override;

        tl::expected<void, std::error_code> bind(const Address &address) override;
        zero::async::coroutine::Task<void, std::error_code> connect(Address address) override;

        [[nodiscard]] FileDescriptor fd() const override;

        void setTimeout(std::chrono::milliseconds timeout) override;
        void setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) override;

    private:
        FileDescriptor mFD;
        std::array<ev::Event, 2> mEvents;
        std::array<std::optional<std::chrono::milliseconds>, 2> mTimeouts;
    };

    tl::expected<Socket, std::error_code> bind(const Address &address);
    tl::expected<Socket, std::error_code> bind(std::span<const Address> addresses);
    tl::expected<Socket, std::error_code> bind(const std::string &ip, unsigned short port);

    zero::async::coroutine::Task<Socket, std::error_code> connect(Address address);
    zero::async::coroutine::Task<Socket, std::error_code> connect(std::span<const Address> addresses);
    zero::async::coroutine::Task<Socket, std::error_code> connect(std::string host, unsigned short port);

    tl::expected<Socket, std::error_code> makeSocket(int family);
}
#endif //ASYNCIO_DGRAM_H

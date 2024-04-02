#ifndef ASYNCIO_STREAM_H
#define ASYNCIO_STREAM_H

#include "net.h"
#include <event2/listener.h>
#include <asyncio/ev/buffer.h>

namespace asyncio::net::stream {
    class IBuffer : public virtual IEndpoint, public virtual asyncio::IBuffer {
    };

    class Buffer : public ev::Buffer, public IBuffer {
    public:
        Buffer(std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev, std::size_t capacity);

        static tl::expected<Buffer, std::error_code>
        make(FileDescriptor fd, std::size_t capacity = DEFAULT_BUFFER_CAPACITY, bool own = true);

        [[nodiscard]] tl::expected<Address, std::error_code> localAddress() const override;
        [[nodiscard]] tl::expected<Address, std::error_code> remoteAddress() const override;
    };

    class Acceptor {
    public:
        explicit Acceptor(evconnlistener *listener);
        Acceptor(Acceptor &&rhs) noexcept;
        Acceptor &operator=(Acceptor &&rhs) noexcept;
        ~Acceptor();

    protected:
        zero::async::coroutine::Task<FileDescriptor, std::error_code> fd();

    public:
        tl::expected<void, std::error_code> close();

    protected:
        std::unique_ptr<evconnlistener, decltype(evconnlistener_free) *> mListener;
        std::optional<Promise<FileDescriptor, std::error_code>> mPromise;
    };

    class Listener : public Acceptor {
    public:
        explicit Listener(evconnlistener *listener);

        zero::async::coroutine::Task<Buffer, std::error_code> accept();
    };

    tl::expected<Listener, std::error_code> listen(const Address &address);
    tl::expected<Listener, std::error_code> listen(std::span<const Address> addresses);
    tl::expected<Listener, std::error_code> listen(const std::string &ip, unsigned short port);

    zero::async::coroutine::Task<Buffer, std::error_code> connect(Address address);
    zero::async::coroutine::Task<Buffer, std::error_code> connect(std::span<const Address> addresses);

    zero::async::coroutine::Task<Buffer, std::error_code>
    connect(std::string host, unsigned short port);

#if __unix__ || __APPLE__
    tl::expected<Listener, std::error_code> listen(const std::string &path);
    zero::async::coroutine::Task<Buffer, std::error_code> connect(std::string path);
#endif
}

#endif //ASYNCIO_STREAM_H

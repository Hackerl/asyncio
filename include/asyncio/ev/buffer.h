#ifndef ASYNCIO_BUFFER_H
#define ASYNCIO_BUFFER_H

#include <event.h>
#include <optional>
#include <asyncio/io.h>

namespace asyncio::ev {
    class Buffer : public virtual IBuffer, public IDeadline, public IFileDescriptor, public Reader, public Writer {
    public:
        explicit Buffer(bufferevent *bev, size_t capacity);
        explicit Buffer(std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev, size_t capacity);
        Buffer(Buffer &&rhs) noexcept;
        ~Buffer() override;

    public:
        void resize(size_t capacity);

    public:
        zero::async::coroutine::Task<void, std::error_code> close() override;

    public:
        zero::async::coroutine::Task<size_t, std::error_code> read(std::span<std::byte> data) override;

    public:
        size_t available() override;
        zero::async::coroutine::Task<std::string, std::error_code> readLine() override;
        zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> readUntil(std::byte byte) override;
        zero::async::coroutine::Task<void, std::error_code> peek(std::span<std::byte> data) override;

    public:
        zero::async::coroutine::Task<size_t, std::error_code> write(std::span<const std::byte> data) override;

    public:
        size_t pending() override;
        zero::async::coroutine::Task<void, std::error_code> flush() override;

    public:
        size_t capacity() override;

    public:
        FileDescriptor fd() override;

    public:
        void setTimeout(std::chrono::milliseconds timeout) override;
        void setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) override;

    private:
        void onClose(const std::error_code &ec);

    private:
        void onBufferRead();
        void onBufferWrite();
        void onBufferEvent(short what);

    private:
        virtual std::error_code getError();

    protected:
        bool mClosed;
        size_t mCapacity;
        std::error_code mLastError;
        std::unique_ptr<bufferevent, void (*)(bufferevent *)> mBev;

    private:
        std::array<std::optional<zero::async::promise::Promise<void, std::error_code>>, 2> mPromises;
    };

    tl::expected<Buffer, std::error_code>
    makeBuffer(FileDescriptor fd, size_t capacity = DEFAULT_BUFFER_CAPACITY, bool own = true);
}

#endif //ASYNCIO_BUFFER_H

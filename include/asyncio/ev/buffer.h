#ifndef ASYNCIO_BUFFER_H
#define ASYNCIO_BUFFER_H

#include <event.h>
#include <optional>
#include <asyncio/io.h>

namespace asyncio::ev {
    class Buffer : public virtual IBuffer, public IDeadline, public IFileDescriptor, public Reader, public Writer {
    protected:
        static constexpr auto READ_INDEX = 0;
        static constexpr auto WRITE_INDEX = 1;

    public:
        explicit Buffer(bufferevent *bev, std::size_t capacity);
        explicit Buffer(std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev, std::size_t capacity);
        Buffer(Buffer &&rhs) noexcept;
        ~Buffer() override;

    private:
        void onEvent(short what);
        void onClose(const std::error_code &ec);

        [[nodiscard]] virtual std::error_code getError() const;

    public:
        void resize(std::size_t capacity);

        zero::async::coroutine::Task<void, std::error_code> close() override;
        zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;

        std::size_t available() override;
        zero::async::coroutine::Task<std::string, std::error_code> readLine() override;
        zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> readUntil(std::byte byte) override;
        zero::async::coroutine::Task<void, std::error_code> peek(std::span<std::byte> data) override;
        zero::async::coroutine::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        std::size_t pending() override;
        zero::async::coroutine::Task<void, std::error_code> flush() override;

        std::size_t capacity() override;
        FileDescriptor fd() override;

        void setTimeout(std::chrono::milliseconds timeout) override;
        void setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) override;

    protected:
        bool mClosed;
        std::size_t mCapacity;
        std::error_code mLastError;
        std::unique_ptr<bufferevent, void (*)(bufferevent *)> mBev;
        std::array<std::optional<zero::async::promise::Promise<void, std::error_code>>, 2> mPromises;
    };

    tl::expected<Buffer, std::error_code>
    makeBuffer(FileDescriptor fd, std::size_t capacity = DEFAULT_BUFFER_CAPACITY, bool own = true);
}

#endif //ASYNCIO_BUFFER_H

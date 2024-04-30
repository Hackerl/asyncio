#ifndef ASYNCIO_EV_BUFFER_H
#define ASYNCIO_EV_BUFFER_H

#include <event.h>
#include <asyncio/io.h>
#include <asyncio/promise.h>

namespace asyncio::ev {
    class Buffer : public virtual IBuffer, public IFileDescriptor, public Reader, public Writer {
    protected:
        static constexpr auto READ_INDEX = 0;
        static constexpr auto WRITE_INDEX = 1;

    public:
        explicit Buffer(std::unique_ptr<bufferevent, void (*)(bufferevent *)> bev, std::size_t capacity);
        Buffer(Buffer &&rhs) noexcept;
        Buffer &operator=(Buffer &&rhs) noexcept;
        ~Buffer() override;

        static tl::expected<Buffer, std::error_code>
        make(FileDescriptor fd, std::size_t capacity = DEFAULT_BUFFER_CAPACITY, bool own = true);

    private:
        void onEvent(short what);
        void onClose(const std::error_code &ec);
        void controlReadEvent(bool enable);
        void controlWriteEvent(bool enable);

        [[nodiscard]] virtual std::error_code getError() const;

    public:
        void resize(std::size_t capacity);

        zero::async::coroutine::Task<void, std::error_code> close() override;
        zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override;

        [[nodiscard]] std::size_t available() const override;
        zero::async::coroutine::Task<std::string, std::error_code> readLine() override;
        zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> readUntil(std::byte byte) override;
        zero::async::coroutine::Task<void, std::error_code> peek(std::span<std::byte> data) override;
        zero::async::coroutine::Task<std::size_t, std::error_code> write(std::span<const std::byte> data) override;

        [[nodiscard]] std::size_t pending() const override;
        zero::async::coroutine::Task<void, std::error_code> flush() override;

        [[nodiscard]] std::size_t capacity() const override;
        [[nodiscard]] FileDescriptor fd() const override;

    protected:
        bool mClosed;
        std::size_t mCapacity;
        std::error_code mLastError;
        std::unique_ptr<bufferevent, void (*)(bufferevent *)> mBev;
        std::array<std::optional<Promise<void, std::error_code>>, 2> mPromises;
    };
}

#endif //ASYNCIO_EV_BUFFER_H

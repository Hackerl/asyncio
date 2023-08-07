#ifndef ASYNCIO_BUFFER_H
#define ASYNCIO_BUFFER_H

#include <event.h>
#include <optional>
#include <asyncio/io.h>

namespace asyncio::ev {
    enum EOL {
        ANY = EVBUFFER_EOL_ANY,
        CRLF = EVBUFFER_EOL_CRLF,
        CRLF_STRICT = EVBUFFER_EOL_CRLF_STRICT,
        LF = EVBUFFER_EOL_LF,
        NUL = EVBUFFER_EOL_NUL
    };

    class IBufferReader : public virtual IReader {
    public:
        virtual size_t available() = 0;
        virtual zero::async::coroutine::Task<std::string, std::error_code> readLine() = 0;
        virtual zero::async::coroutine::Task<std::string, std::error_code> readLine(EOL eol) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> peek(std::span<std::byte> data) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> readExactly(std::span<std::byte> data) = 0;
    };

    class IBufferWriter : public virtual IWriter {
    public:
        virtual tl::expected<void, std::error_code> writeLine(std::string_view line) = 0;
        virtual tl::expected<void, std::error_code> writeLine(std::string_view line, EOL eol) = 0;
        virtual tl::expected<void, std::error_code> submit(std::span<const std::byte> data) = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> drain() = 0;

    public:
        virtual size_t pending() = 0;
        virtual zero::async::coroutine::Task<void, std::error_code> waitClosed() = 0;
    };

    class IBuffer : public virtual IStreamIO, public IDeadline, public IBufferReader, public IBufferWriter {
    public:
        virtual evutil_socket_t fd() = 0;
    };

    class Buffer : public virtual IBuffer {
    public:
        explicit Buffer(bufferevent *bev);
        Buffer(Buffer &&rhs) noexcept;
        ~Buffer() override;

    public:
        size_t available() override;
        zero::async::coroutine::Task<std::string, std::error_code> readLine() override;
        zero::async::coroutine::Task<std::string, std::error_code> readLine(EOL eol) override;
        zero::async::coroutine::Task<void, std::error_code> peek(std::span<std::byte> data) override;
        zero::async::coroutine::Task<void, std::error_code> readExactly(std::span<std::byte> data) override;

    public:
        tl::expected<void, std::error_code> writeLine(std::string_view line) override;
        tl::expected<void, std::error_code> writeLine(std::string_view line, EOL eol) override;
        tl::expected<void, std::error_code> submit(std::span<const std::byte> data) override;
        zero::async::coroutine::Task<void, std::error_code> drain() override;

    public:
        size_t pending() override;
        zero::async::coroutine::Task<void, std::error_code> waitClosed() override;

    public:
        evutil_socket_t fd() override;

    public:
        zero::async::coroutine::Task<size_t, std::error_code> read(std::span<std::byte> data) override;
        zero::async::coroutine::Task<void, std::error_code> write(std::span<const std::byte> data) override;
        tl::expected<void, std::error_code> close() override;

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
        std::unique_ptr<bufferevent, decltype(bufferevent_free) *> mBev;

    private:
        std::array<std::optional<zero::async::promise::Promise<void, std::error_code>>, 3> mPromises;
    };

    tl::expected<Buffer, std::error_code> makeBuffer(evutil_socket_t fd, bool own = true);
}

#endif //ASYNCIO_BUFFER_H

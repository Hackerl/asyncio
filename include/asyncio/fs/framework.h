#ifndef ASYNCIO_FRAMEWORK_H
#define ASYNCIO_FRAMEWORK_H

#include <asyncio/io.h>

namespace asyncio {
    class EventLoop;

    namespace fs {
        class IFramework : public zero::Interface {
        public:
            virtual tl::expected<void, std::error_code> associate(FileDescriptor fd) = 0;

            virtual zero::async::coroutine::Task<std::size_t, std::error_code>
            read(
                std::shared_ptr<EventLoop> eventLoop,
                FileDescriptor fd,
                std::streamoff offset,
                std::span<std::byte> data
            ) = 0;

            virtual zero::async::coroutine::Task<std::size_t, std::error_code>
            write(
                std::shared_ptr<EventLoop> eventLoop,
                FileDescriptor fd,
                std::streamoff offset,
                std::span<const std::byte> data
            ) = 0;
        };
    }
}

#endif //ASYNCIO_FRAMEWORK_H

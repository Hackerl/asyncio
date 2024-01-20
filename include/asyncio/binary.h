#ifndef ASYNCIO_BINARY_H
#define ASYNCIO_BINARY_H

#include <asyncio/io.h>
#include <zero/expect.h>

namespace asyncio::binary {
    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<T, std::error_code> readLE(IReader &reader) {
        std::byte data[sizeof(T)];
        CO_EXPECT(co_await reader.readExactly(data));

        T v = 0;

        for (std::size_t i = 0; i < sizeof(T); i++)
            v |= static_cast<T>(*(data + i)) << i * 8;

        co_return v;
    }

    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<T, std::error_code> readBE(IReader &reader) {
        std::byte data[sizeof(T)];
        CO_EXPECT(co_await reader.readExactly(data));

        T v = 0;

        for (std::size_t i = 0; i < sizeof(T); i++)
            v |= static_cast<T>(*(data + i)) << (sizeof(T) - i - 1) * 8;

        co_return v;
    }

    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<void, std::error_code> writeLE(IWriter &writer, T value) {
        std::byte data[sizeof(T)];

        for (std::size_t i = 0; i < sizeof(T); i++)
            data[i] = static_cast<std::byte>(value >> i * 8);

        co_return co_await writer.writeAll(data);
    }

    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<void, std::error_code> writeBE(IWriter &writer, T value) {
        std::byte data[sizeof(T)];

        for (std::size_t i = 0; i < sizeof(T); i++)
            data[i] = static_cast<std::byte>(value >> (sizeof(T) - i - 1) * 8);

        co_return co_await writer.writeAll(data);
    }
}

#endif //ASYNCIO_BINARY_H

#ifndef ASYNCIO_BINARY_H
#define ASYNCIO_BINARY_H

#include <asyncio/io.h>
#include <zero/expect.h>

namespace asyncio::binary {
    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<T, std::error_code> readLE(IReader &reader) {
        std::array<std::byte, sizeof(T)> bytes = {};
        CO_EXPECT(co_await reader.readExactly(bytes));

        T v = 0;

        for (std::size_t i = 0; i < sizeof(T); ++i)
            v |= static_cast<T>(bytes[i]) << i * 8;

        co_return v;
    }

    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<T, std::error_code> readBE(IReader &reader) {
        std::array<std::byte, sizeof(T)> bytes = {};
        CO_EXPECT(co_await reader.readExactly(bytes));

        T v = 0;

        for (std::size_t i = 0; i < sizeof(T); ++i)
            v |= static_cast<T>(bytes[i]) << (sizeof(T) - i - 1) * 8;

        co_return v;
    }

    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<void, std::error_code> writeLE(IWriter &writer, const T value) {
        std::array<std::byte, sizeof(T)> bytes = {};

        for (std::size_t i = 0; i < sizeof(T); ++i)
            bytes[i] = static_cast<std::byte>(value >> i * 8);

        co_return co_await writer.writeAll(bytes);
    }

    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<void, std::error_code> writeBE(IWriter &writer, const T value) {
        std::array<std::byte, sizeof(T)> bytes = {};

        for (std::size_t i = 0; i < sizeof(T); ++i)
            bytes[i] = static_cast<std::byte>(value >> (sizeof(T) - i - 1) * 8);

        co_return co_await writer.writeAll(bytes);
    }
}

#endif //ASYNCIO_BINARY_H

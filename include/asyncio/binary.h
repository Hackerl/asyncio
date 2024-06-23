#ifndef ASYNCIO_BINARY_H
#define ASYNCIO_BINARY_H

#include <asyncio/io.h>

namespace asyncio::binary {
    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    task::Task<T, std::error_code> readLE(Reader auto &reader) {
        std::array<std::byte, sizeof(T)> bytes = {};
        CO_EXPECT(co_await std::invoke(&IReader::readExactly, reader, bytes));

        T v = 0;

        for (std::size_t i = 0; i < sizeof(T); ++i)
            v |= static_cast<T>(bytes[i]) << i * 8;

        co_return v;
    }

    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    task::Task<T, std::error_code> readBE(Reader auto &reader) {
        std::array<std::byte, sizeof(T)> bytes = {};
        CO_EXPECT(co_await std::invoke(&IReader::readExactly, reader, bytes));

        T v = 0;

        for (std::size_t i = 0; i < sizeof(T); ++i)
            v |= static_cast<T>(bytes[i]) << (sizeof(T) - i - 1) * 8;

        co_return v;
    }

    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    task::Task<void, std::error_code> writeLE(Writer auto &writer, const T value) {
        std::array<std::byte, sizeof(T)> bytes = {};

        for (std::size_t i = 0; i < sizeof(T); ++i)
            bytes[i] = static_cast<std::byte>(value >> i * 8);

        co_return co_await std::invoke(&IWriter::writeAll, writer, bytes);
    }

    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    task::Task<void, std::error_code> writeBE(Writer auto &writer, const T value) {
        std::array<std::byte, sizeof(T)> bytes = {};

        for (std::size_t i = 0; i < sizeof(T); ++i)
            bytes[i] = static_cast<std::byte>(value >> (sizeof(T) - i - 1) * 8);

        co_return co_await std::invoke(&IWriter::writeAll, writer, bytes);
    }
}

#endif //ASYNCIO_BINARY_H

#ifndef ASYNCIO_BINARY_H
#define ASYNCIO_BINARY_H

#include <asyncio/io.h>
#include <zero/try.h>

namespace asyncio::binary {
    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<T, std::error_code> readLE(IReader &reader) {
        std::byte data[sizeof(T)];
        CO_TRY(co_await reader.readExactly(data));

        T v = 0;

        for (std::size_t i = 0; i < sizeof(T); i++)
            v |= static_cast<T>(*(data + i)) << i * 8;

        co_return v;
    }

    template<typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<T, std::error_code> readBE(IReader &reader) {
        std::byte data[sizeof(T)];
        CO_TRY(co_await reader.readExactly(data));

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

    template<typename T, typename R>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1 && std::derived_from<R, IReader>)
    zero::async::coroutine::Task<T, std::error_code> readLE(R &&reader) {
        auto r = std::forward<R>(reader);
        co_return co_await readLE<T>(r);
    }

    template<typename T, typename R>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1 && std::derived_from<R, IReader>)
    zero::async::coroutine::Task<T, std::error_code> readBE(R &&reader) {
        auto r = std::forward<R>(reader);
        co_return co_await readBE<T>(r);
    }

    template<typename W, typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1 && std::derived_from<W, IWriter>)
    zero::async::coroutine::Task<void, std::error_code> writeLE(W &&writer, T value) {
        auto w = std::forward<W>(writer);
        co_return co_await writeLE<T>(w, value);
    }

    template<typename W, typename T>
        requires (std::is_arithmetic_v<T> && sizeof(T) > 1 && std::derived_from<W, IWriter>)
    zero::async::coroutine::Task<void, std::error_code> writeBE(W &&writer, T value) {
        auto w = std::forward<W>(writer);
        co_return co_await writeBE<T>(w, value);
    }
}

#endif //ASYNCIO_BINARY_H

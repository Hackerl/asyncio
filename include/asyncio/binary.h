#ifndef ASYNCIO_BINARY_H
#define ASYNCIO_BINARY_H

#include <asyncio/io.h>
#include <zero/try.h>

namespace asyncio::binary {
    template<typename T, typename R>
    requires (std::is_arithmetic_v<T> && sizeof(T) > 1 && std::derived_from<R, IReader>)
    zero::async::coroutine::Task<T, std::error_code> readLE(R reader) {
        std::byte data[sizeof(T)];
        CO_TRY(co_await readExactly(std::move(reader), data));

        T v = 0;

        for (size_t i = 0; i < sizeof(T); i++)
            v |= ((T) *(data + i)) << (i * 8);

        co_return v;
    }

    template<typename T, typename R>
    requires (std::is_arithmetic_v<T> && sizeof(T) > 1 && std::derived_from<R, IReader>)
    zero::async::coroutine::Task<T, std::error_code> readBE(R reader) {
        std::byte data[sizeof(T)];
        CO_TRY(co_await readExactly(std::move(reader), data));

        T v = 0;

        for (size_t i = 0; i < sizeof(T); i++)
            v |= ((T) *(data + i)) << ((sizeof(T) - i - 1) * 8);

        co_return v;
    }

    template<typename W, typename T>
    requires (std::is_arithmetic_v<T> && sizeof(T) > 1 && std::derived_from<W, IWriter>)
    zero::async::coroutine::Task<void, std::error_code> writeLE(W writer, T value) {
        std::byte data[sizeof(T)];

        for (size_t i = 0; i < sizeof(T); i++)
            data[i] = (std::byte) (value >> (i * 8));

        co_return co_await writer.write(data);
    }

    template<typename W, typename T>
    requires (std::is_arithmetic_v<T> && sizeof(T) > 1 && std::derived_from<W, IWriter>)
    zero::async::coroutine::Task<void, std::error_code> writeBE(W writer, T value) {
        std::byte data[sizeof(T)];

        for (size_t i = 0; i < sizeof(T); i++)
            data[i] = (std::byte) (value >> ((sizeof(T) - i - 1) * 8));

        co_return co_await writer.write(data);
    }

    template<typename T>
    requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<T, std::error_code> readLE(std::shared_ptr<IReader> reader) {
        std::byte data[sizeof(T)];
        CO_TRY(co_await readExactly(std::move(reader), data));

        T v = 0;

        for (size_t i = 0; i < sizeof(T); i++)
            v |= ((T) *(data + i)) << (i * 8);

        co_return v;
    }

    template<typename T>
    requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<T, std::error_code> readBE(std::shared_ptr<IReader> reader) {
        std::byte data[sizeof(T)];
        CO_TRY(co_await readExactly(std::move(reader), data));

        T v = 0;

        for (size_t i = 0; i < sizeof(T); i++)
            v |= ((T) *(data + i)) << ((sizeof(T) - i - 1) * 8);

        co_return v;
    }

    template<typename T>
    requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<void, std::error_code> writeLE(std::shared_ptr<IWriter> writer, T value) {
        std::byte data[sizeof(T)];

        for (size_t i = 0; i < sizeof(T); i++)
            data[i] = (std::byte) (value >> (i * 8));

        co_return co_await writer->write(data);
    }

    template<typename T>
    requires (std::is_arithmetic_v<T> && sizeof(T) > 1)
    zero::async::coroutine::Task<void, std::error_code> writeBE(std::shared_ptr<IWriter> writer, T value) {
        std::byte data[sizeof(T)];

        for (size_t i = 0; i < sizeof(T); i++)
            data[i] = (std::byte) (value >> ((sizeof(T) - i - 1) * 8));

        co_return co_await writer->write(data);
    }
}

#endif //ASYNCIO_BINARY_H

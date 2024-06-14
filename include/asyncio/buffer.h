#ifndef ASYNCIO_BUFFER_H
#define ASYNCIO_BUFFER_H

#include "io.h"
#include <cassert>
#include <zero/expect.h>

namespace asyncio {
    template<typename T>
        requires (
            std::derived_from<T, IReader> ||
            std::convertible_to<T, std::unique_ptr<IReader>> ||
            std::convertible_to<T, std::shared_ptr<IReader>>
        )
    class BufReader final : public IBufReader, public Reader {
    public:
        explicit BufReader(T reader, const std::size_t capacity = DEFAULT_BUFFER_CAPACITY)
            : mReader(std::move(reader)), mCapacity(capacity), mHead(0), mTail(0),
              mBuffer(std::make_unique<std::byte[]>(capacity)) {
        }

    private:
        zero::async::coroutine::Task<std::size_t, std::error_code> rawRead(const std::span<std::byte> data) {
            if constexpr (std::derived_from<T, IReader>) {
                return mReader.read(data);
            }
            else {
                return mReader->read(data);
            }
        }

    public:
        zero::async::coroutine::Task<std::size_t, std::error_code> read(const std::span<std::byte> data) override {
            if (available() == 0) {
                if (data.size() >= mCapacity)
                    co_return co_await rawRead(data);

                mHead = 0;
                mTail = 0;

                const auto n = co_await rawRead({mBuffer.get(), mCapacity});
                CO_EXPECT(n);

                if (*n == 0)
                    co_return 0;

                mTail = *n;
            }

            const std::size_t size = (std::min)(available(), data.size());

            std::copy_n(mBuffer.get() + mHead, size, data.data());
            mHead += size;

            co_return size;
        }

        [[nodiscard]] std::size_t capacity() const override {
            return mCapacity;
        }

        [[nodiscard]] std::size_t available() const override {
            return mTail - mHead;
        }

        zero::async::coroutine::Task<std::string, std::error_code> readLine() override {
            auto data = co_await readUntil(std::byte{'\n'});
            CO_EXPECT(data);

            if (data->back() == std::byte{'\r'})
                data->pop_back();

            co_return std::string{reinterpret_cast<const char *>(data->data()), data->size()};
        }

        zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> readUntil(const std::byte byte) override {
            std::expected<std::vector<std::byte>, std::error_code> result;

            while (true) {
                const auto first = mBuffer.get() + mHead;
                const auto last = mBuffer.get() + mTail;

                if (const auto it = std::find(first, last, byte); it != last) {
                    std::copy(first, it, std::back_inserter(*result));
                    mHead += std::distance(first, it) + 1;
                    break;
                }

                std::copy(first, last, std::back_inserter(*result));

                mHead = 0;
                mTail = 0;

                const auto n = co_await rawRead({mBuffer.get(), mCapacity});
                CO_EXPECT(n);

                if (*n == 0) {
                    result = std::unexpected<std::error_code>(IOError::UNEXPECTED_EOF);
                    break;
                }

                mTail = *n;
            }

            co_return result;
        }

        zero::async::coroutine::Task<void, std::error_code> peek(const std::span<std::byte> data) override {
            if (data.size() > mCapacity)
                co_return std::unexpected(IOError::INVALID_ARGUMENT);

            if (const std::size_t available = this->available(); available < data.size()) {
                if (mHead > 0) {
                    std::copy(mBuffer.get() + mHead, mBuffer.get() + mTail, mBuffer.get());
                    mHead = 0;
                    mTail = available;
                }

                while (mTail < data.size()) {
                    const auto n = co_await rawRead({mBuffer.get() + mTail, mCapacity - mTail});
                    CO_EXPECT(n);

                    if (*n == 0)
                        co_return std::unexpected<std::error_code>(IOError::UNEXPECTED_EOF);

                    mTail += *n;
                }
            }

            assert(available() >= data.size());
            std::copy_n(mBuffer.get() + mHead, data.size(), data.data());
            co_return {};
        }

    private:
        T mReader;
        std::size_t mCapacity;
        std::size_t mHead;
        std::size_t mTail;
        std::unique_ptr<std::byte[]> mBuffer;
    };

    template<typename T>
        requires (
            std::derived_from<T, IWriter> ||
            std::convertible_to<T, std::unique_ptr<IWriter>> ||
            std::convertible_to<T, std::shared_ptr<IWriter>>
        )
    class BufWriter final : public IBufWriter, public Writer {
    public:
        explicit BufWriter(T writer, const std::size_t capacity = DEFAULT_BUFFER_CAPACITY)
            : mWriter(std::move(writer)), mCapacity(capacity), mPending(0),
              mBuffer(std::make_unique<std::byte[]>(capacity)) {
        }

    private:
        zero::async::coroutine::Task<std::size_t, std::error_code> rawWrite(const std::span<const std::byte> data) {
            if constexpr (std::derived_from<T, IWriter>) {
                return mWriter.write(data);
            }
            else {
                return mWriter->write(data);
            }
        }

    public:
        zero::async::coroutine::Task<std::size_t, std::error_code>
        write(const std::span<const std::byte> data) override {
            std::expected<std::size_t, std::error_code> result;

            while (*result < data.size()) {
                assert(mPending <= mCapacity);

                if (mPending == mCapacity) {
                    if (const auto res = co_await flush(); !res) {
                        if (*result > 0)
                            break;

                        result = std::unexpected(res.error());
                        break;
                    }

                    continue;
                }

                const std::size_t n = (std::min)(mCapacity - mPending, data.size() - *result);
                std::copy_n(data.data() + *result, n, mBuffer.get() + mPending);

                mPending += n;
                *result += n;
            }

            co_return result;
        }

        [[nodiscard]] std::size_t capacity() const override {
            return mCapacity;
        }

        [[nodiscard]] std::size_t pending() const override {
            return mPending;
        }

        zero::async::coroutine::Task<void, std::error_code> flush() override {
            std::expected<void, std::error_code> result;
            std::size_t offset = 0;

            while (offset < mPending) {
                if (co_await zero::async::coroutine::cancelled) {
                    result = std::unexpected<std::error_code>(zero::async::coroutine::Error::CANCELLED);
                    break;
                }

                const auto n = co_await rawWrite({mBuffer.get() + offset, mPending - offset});

                if (!n) {
                    result = std::unexpected(n.error());
                    break;
                }

                offset += *n;
            }

            if (offset > 0 && offset < mPending)
                std::copy(mBuffer.get() + offset, mBuffer.get() + mPending, mBuffer.get());

            mPending -= offset;
            co_return result;
        }

    private:
        T mWriter;
        std::size_t mCapacity;
        std::size_t mPending;
        std::unique_ptr<std::byte[]> mBuffer;
    };
}

#endif //ASYNCIO_BUFFER_H

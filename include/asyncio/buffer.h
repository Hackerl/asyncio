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
    class BufReader : public IBufReader, public Reader {
    public:
        explicit BufReader(T reader, const std::size_t capacity = DEFAULT_BUFFER_CAPACITY):
            mReader(std::move(reader)), mCapacity(capacity), mHead(0), mTail(0),
            mBuffer(std::make_unique<std::byte[]>(capacity)) {
        }

    private:
        zero::async::coroutine::Task<std::size_t, std::error_code> rawRead(std::span<std::byte> data) {
            if constexpr (std::derived_from<T, IReader>) {
                return mReader.read(data);
            }
            else {
                return mReader->read(data);
            }
        }

    public:
        zero::async::coroutine::Task<std::size_t, std::error_code> read(std::span<std::byte> data) override {
            if (available() == 0) {
                if (data.size() >= mCapacity)
                    co_return co_await rawRead(data);

                mHead = 0;
                mTail = 0;

                const auto n = co_await rawRead({mBuffer.get(), mCapacity});
                CO_EXPECT(n);
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
            tl::expected<std::vector<std::byte>, std::error_code> result;

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

                mTail = *n;
            }

            co_return result;
        }

        zero::async::coroutine::Task<void, std::error_code> peek(std::span<std::byte> data) override {
            if (data.size() > mCapacity)
                co_return tl::unexpected(make_error_code(std::errc::invalid_argument));

            if (const std::size_t available = this->available(); available < data.size()) {
                if (mHead > 0) {
                    std::copy(mBuffer.get() + mHead, mBuffer.get() + mTail, mBuffer.get());
                    mHead = 0;
                    mTail = available;
                }

                while (mTail < data.size()) {
                    const auto n = co_await rawRead({mBuffer.get() + mTail, mCapacity - mTail});
                    CO_EXPECT(n);
                    mTail += *n;
                }
            }

            assert(available() >= data.size());
            std::copy_n(mBuffer.get() + mHead, data.size(), data.data());
            co_return tl::expected<void, std::error_code>{};
        }

    private:
        T mReader;
        std::size_t mCapacity;
        std::size_t mHead;
        std::size_t mTail;
        std::unique_ptr<std::byte[]> mBuffer;
    };
}

#endif //ASYNCIO_BUFFER_H

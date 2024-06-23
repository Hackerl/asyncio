#ifndef ASYNCIO_BUFFER_H
#define ASYNCIO_BUFFER_H

#include "io.h"

namespace asyncio {
    DEFINE_ERROR_CODE_EX(
        BufReaderError,
        "asyncio::BufReader",
        INVALID_ARGUMENT, "invalid argument", std::errc::invalid_argument,
        UNEXPECTED_EOF, "unexpected end of file", IOError::UNEXPECTED_EOF
    )

    template<Reader T>
    class BufReader final : public IBufReader {
        static constexpr auto DEFAULT_BUFFER_CAPACITY = 8192;

    public:
        explicit BufReader(T reader, const std::size_t capacity = DEFAULT_BUFFER_CAPACITY)
            : mReader(std::move(reader)), mCapacity(capacity), mHead(0), mTail(0),
              mBuffer(std::make_unique<std::byte[]>(capacity)) {
        }

        [[nodiscard]] std::size_t capacity() const {
            return mCapacity;
        }

        task::Task<std::size_t, std::error_code> read(const std::span<std::byte> data) override {
            if (available() == 0) {
                if (data.size() >= mCapacity)
                    co_return co_await std::invoke(&IReader::read, mReader, data);

                mHead = 0;
                mTail = 0;

                const auto n = co_await std::invoke(&IReader::read, mReader, std::span{mBuffer.get(), mCapacity});
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

        [[nodiscard]] std::size_t available() const override {
            return mTail - mHead;
        }

        task::Task<std::string, std::error_code> readLine() override {
            auto data = co_await readUntil(std::byte{'\n'});
            CO_EXPECT(data);

            if (data->back() == std::byte{'\r'})
                data->pop_back();

            co_return std::string{reinterpret_cast<const char *>(data->data()), data->size()};
        }

        task::Task<std::vector<std::byte>, std::error_code> readUntil(const std::byte byte) override {
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

                const auto n = co_await std::invoke(&IReader::read, mReader, std::span{mBuffer.get(), mCapacity});
                CO_EXPECT(n);

                if (*n == 0) {
                    result = std::unexpected(make_error_code(BufReaderError::UNEXPECTED_EOF));
                    break;
                }

                mTail = *n;
            }

            co_return result;
        }

        task::Task<void, std::error_code> peek(const std::span<std::byte> data) override {
            if (data.size() > mCapacity)
                co_return std::unexpected(make_error_code(BufReaderError::INVALID_ARGUMENT));

            if (const std::size_t available = this->available(); available < data.size()) {
                if (mHead > 0) {
                    std::copy(mBuffer.get() + mHead, mBuffer.get() + mTail, mBuffer.get());
                    mHead = 0;
                    mTail = available;
                }

                while (mTail < data.size()) {
                    const auto n = co_await std::invoke(
                        &IReader::read,
                        mReader,
                        std::span{mBuffer.get() + mTail, mCapacity - mTail}
                    );
                    CO_EXPECT(n);

                    if (*n == 0)
                        co_return std::unexpected(make_error_code(BufReaderError::UNEXPECTED_EOF));

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

    template<Writer T>
    class BufWriter final : public IBufWriter {
        static constexpr auto DEFAULT_BUFFER_CAPACITY = 8192;

    public:
        explicit BufWriter(T writer, const std::size_t capacity = DEFAULT_BUFFER_CAPACITY)
            : mWriter(std::move(writer)), mCapacity(capacity), mPending(0),
              mBuffer(std::make_unique<std::byte[]>(capacity)) {
        }

        [[nodiscard]] std::size_t capacity() const {
            return mCapacity;
        }

        task::Task<std::size_t, std::error_code>
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

        [[nodiscard]] std::size_t pending() const override {
            return mPending;
        }

        task::Task<void, std::error_code> flush() override {
            std::expected<void, std::error_code> result;
            std::size_t offset = 0;

            while (offset < mPending) {
                if (co_await task::cancelled) {
                    result = std::unexpected<std::error_code>(task::Error::CANCELLED);
                    break;
                }

                const auto n = co_await std::invoke(
                    &IWriter::write,
                    mWriter,
                    std::span{mBuffer.get() + offset, mPending - offset}
                );

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

DECLARE_ERROR_CODES(asyncio::BufReaderError)

#endif //ASYNCIO_BUFFER_H

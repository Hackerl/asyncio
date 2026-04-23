#ifndef ASYNCIO_BUFFER_H
#define ASYNCIO_BUFFER_H

#include "io.h"

namespace asyncio {
    Z_DEFINE_ERROR_CODE_EX(
        BufReaderError,
        "asyncio::BufReader",
        InvalidArgument, "Invalid argument", std::errc::invalid_argument,
        UnexpectedEOF, "Unexpected end of file", IOError::UnexpectedEOF
    )

    template<zero::meta::Trait<IReader> T>
    class BufReader final : public IBufReader {
        static constexpr auto DefaultBufferCapacity = 8192;

    public:
        explicit BufReader(T reader, const std::size_t capacity = DefaultBufferCapacity)
            : mReader{std::move(reader)}, mCapacity{capacity}, mHead{0}, mTail{0},
              mBuffer{std::make_unique<std::byte[]>(capacity)} {
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
                Z_CO_EXPECT(n);

                if (*n == 0)
                    co_return 0;

                mTail = *n;
            }

            const auto size = (std::min)(available(), data.size());

            std::copy_n(mBuffer.get() + mHead, size, data.begin());
            mHead += size;

            co_return size;
        }

        [[nodiscard]] std::size_t available() const override {
            return mTail - mHead;
        }

        task::Task<std::string, std::error_code> readLine() override {
            auto data = co_await readUntil(std::byte{'\n'});
            Z_CO_EXPECT(data);

            if (!data->empty() && data->back() == std::byte{'\r'})
                data->pop_back();

            co_return std::string{reinterpret_cast<const char *>(data->data()), data->size()};
        }

        task::Task<std::vector<std::byte>, std::error_code> readUntil(const std::byte byte) override {
            std::vector<std::byte> data;

            while (true) {
                if (co_await task::cancelled)
                    co_return std::unexpected{task::Error::Cancelled};

                const auto first = mBuffer.get() + mHead;
                const auto last = mBuffer.get() + mTail;

                if (const auto it = std::find(first, last, byte); it != last) {
                    data.append_range(std::ranges::subrange{first, it});
                    mHead += std::distance(first, it) + 1;
                    break;
                }

                data.append_range(std::ranges::subrange{first, last});

                mHead = 0;
                mTail = 0;

                const auto n = co_await std::invoke(&IReader::read, mReader, std::span{mBuffer.get(), mCapacity});
                Z_CO_EXPECT(n);

                if (*n == 0)
                    co_return std::unexpected{make_error_code(BufReaderError::UnexpectedEOF)};

                mTail = *n;
            }

            co_return data;
        }

        task::Task<void, std::error_code> peek(const std::span<std::byte> data) override {
            if (data.size() > mCapacity)
                co_return std::unexpected{make_error_code(BufReaderError::InvalidArgument)};

            if (const auto available = this->available(); available < data.size()) {
                if (mHead > 0) {
                    std::copy(mBuffer.get() + mHead, mBuffer.get() + mTail, mBuffer.get());
                    mHead = 0;
                    mTail = available;
                }

                while (mTail < data.size()) {
                    if (co_await task::cancelled)
                        co_return std::unexpected{task::Error::Cancelled};

                    const auto n = co_await std::invoke(
                        &IReader::read,
                        mReader,
                        std::span{mBuffer.get() + mTail, mCapacity - mTail}
                    );
                    Z_CO_EXPECT(n);

                    if (*n == 0)
                        co_return std::unexpected{make_error_code(BufReaderError::UnexpectedEOF)};

                    mTail += *n;
                }
            }

            assert(available() >= data.size());
            std::copy_n(mBuffer.get() + mHead, data.size(), data.begin());
            co_return {};
        }

    private:
        T mReader;
        std::size_t mCapacity;
        std::size_t mHead;
        std::size_t mTail;
        std::unique_ptr<std::byte[]> mBuffer;
    };

    template<zero::meta::Trait<IWriter> T>
    class BufWriter final : public IBufWriter {
        static constexpr auto DefaultBufferCapacity = 8192;

    public:
        explicit BufWriter(T writer, const std::size_t capacity = DefaultBufferCapacity)
            : mWriter{std::move(writer)}, mCapacity{capacity}, mPending{0},
              mBuffer{std::make_unique<std::byte[]>(capacity)} {
        }

    private:
        task::Task<std::size_t, std::error_code> writeOnce(const std::span<const std::byte> data) {
            assert(mPending <= mCapacity);

            if (mPending == mCapacity) {
                Z_CO_EXPECT(co_await flush());
            }

            const auto size = (std::min)(mCapacity - mPending, data.size());
            std::copy_n(data.begin(), size, mBuffer.get() + mPending);

            mPending += size;
            co_return size;
        }

    public:
        [[nodiscard]] std::size_t capacity() const {
            return mCapacity;
        }

        task::Task<std::size_t, std::error_code>
        write(const std::span<const std::byte> data) override {
            std::size_t offset{0};

            while (offset < data.size()) {
                assert(mPending <= mCapacity);

                if (co_await task::cancelled) {
                    if (offset > 0)
                        break;

                    co_return std::unexpected{task::Error::Cancelled};
                }

                const auto n = co_await writeOnce(data.subspan(offset));

                if (!n) {
                    if (offset > 0)
                        break;

                    co_return std::unexpected{n.error()};
                }

                assert(*n != 0);
                offset += *n;
            }

            co_return offset;
        }

        [[nodiscard]] std::size_t pending() const override {
            return mPending;
        }

        task::Task<void, std::error_code> flush() override {
            std::expected<void, std::error_code> result;
            std::size_t offset{0};

            while (offset < mPending) {
                if (co_await task::cancelled) {
                    result = std::unexpected{task::Error::Cancelled};
                    break;
                }

                const auto n = co_await std::invoke(
                    &IWriter::write,
                    mWriter,
                    std::span{mBuffer.get() + offset, mPending - offset}
                );

                if (!n) {
                    result = std::unexpected{n.error()};
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

Z_DECLARE_ERROR_CODES(asyncio::BufReaderError)

#endif //ASYNCIO_BUFFER_H

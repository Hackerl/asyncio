#include <asyncio/io.h>

asyncio::task::Task<void, std::error_code> asyncio::IReader::readExactly(const std::span<std::byte> data) {
    std::size_t offset{0};

    while (offset < data.size()) {
        const auto n = co_await read(data.subspan(offset));
        CO_EXPECT(n);

        if (*n == 0)
            co_return std::unexpected{ReadExactlyError::UNEXPECTED_EOF};

        offset += *n;
    }

    co_return {};
}

asyncio::task::Task<std::vector<std::byte>, std::error_code> asyncio::IReader::readAll() {
    std::vector<std::byte> data;

    while (true) {
        std::array<std::byte, 10240> buffer; // NOLINT(*-pro-type-member-init)

        const auto n = co_await read(buffer);
        CO_EXPECT(n);

        if (*n == 0)
            break;

        data.insert(data.end(), buffer.begin(), buffer.begin() + *n);
    }

    co_return data;
}

asyncio::task::Task<void, std::error_code> asyncio::IWriter::writeAll(const std::span<const std::byte> data) {
    std::size_t offset{0};

    while (offset < data.size()) {
        if (co_await task::cancelled)
            co_return std::unexpected{task::Error::CANCELLED};

        const auto n = co_await write(data.subspan(offset));
        CO_EXPECT(n);

        assert(*n != 0);
        offset += *n;
    }

    co_return {};
}

asyncio::task::Task<void, std::error_code> asyncio::ISeekable::rewind() {
    CO_EXPECT(co_await seek(0, Whence::BEGIN));
    co_return {};
}

asyncio::task::Task<std::uint64_t, std::error_code> asyncio::ISeekable::length() {
    const auto pos = co_await position();
    CO_EXPECT(pos);

    const auto length = co_await seek(0, Whence::END);
    CO_EXPECT(length);

    CO_EXPECT(co_await seek(*pos, Whence::BEGIN));
    co_return *length;
}

asyncio::task::Task<std::uint64_t, std::error_code> asyncio::ISeekable::position() {
    return seek(0, Whence::CURRENT);
}

DEFINE_ERROR_CATEGORY_INSTANCES(
    asyncio::IOError,
    asyncio::IReader::ReadExactlyError
)

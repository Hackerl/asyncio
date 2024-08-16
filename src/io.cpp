#include <asyncio/io.h>

asyncio::task::Task<void, std::error_code> asyncio::IReader::readExactly(const std::span<std::byte> data) {
    std::expected<void, std::error_code> result;
    std::size_t offset = 0;

    while (offset < data.size()) {
        const auto n = co_await read(data.subspan(offset));

        if (!n) {
            result = std::unexpected(n.error());
            break;
        }

        if (*n == 0) {
            result = std::unexpected<std::error_code>(ReadExactlyError::UNEXPECTED_EOF);
            break;
        }

        offset += *n;
    }

    co_return result;
}

asyncio::task::Task<std::vector<std::byte>, std::error_code> asyncio::IReader::readAll() {
    std::expected<std::vector<std::byte>, std::error_code> result;

    while (true) {
        std::array<std::byte, 10240> data; // NOLINT(*-pro-type-member-init)
        const auto n = co_await read(data);

        if (!n) {
            result = std::unexpected(n.error());
            break;
        }

        if (*n == 0)
            break;

        std::copy_n(data.begin(), *n, std::back_inserter(*result));
    }

    co_return result;
}

asyncio::task::Task<void, std::error_code> asyncio::IWriter::writeAll(const std::span<const std::byte> data) {
    std::expected<void, std::error_code> result;
    std::size_t offset = 0;

    while (offset < data.size()) {
        if (co_await task::cancelled) {
            result = std::unexpected<std::error_code>(task::Error::CANCELLED);
            break;
        }

        const auto n = co_await write(data.subspan(offset));

        if (!n) {
            result = std::unexpected(n.error());
            break;
        }

        assert(*n != 0);
        offset += *n;
    }

    co_return result;
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

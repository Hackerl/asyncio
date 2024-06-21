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
            result = std::unexpected<std::error_code>(Error::UNEXPECTED_EOF);
            break;
        }

        offset += *n;
    }

    co_return result;
}

asyncio::task::Task<std::vector<std::byte>, std::error_code> asyncio::IReader::readAll() {
    std::expected<std::vector<std::byte>, std::error_code> result;

    while (true) {
        std::array<std::byte, 10240> data = {};
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

asyncio::task::Task<void, std::error_code> asyncio::copy(IReader &reader, IWriter &writer) {
    std::expected<void, std::error_code> result;

    while (true) {
        if (co_await task::cancelled) {
            result = std::unexpected<std::error_code>(task::Error::CANCELLED);
            break;
        }

        std::array<std::byte, 10240> data = {};
        const auto n = co_await reader.read(data);

        if (!n) {
            result = std::unexpected(n.error());
            break;
        }

        if (*n == 0)
            break;

        co_await task::lock;

        if (const auto res = co_await writer.writeAll({data.data(), *n}); !res) {
            result = std::unexpected(res.error());
            break;
        }

        co_await task::unlock;
    }

    co_return result;
}

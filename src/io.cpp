#include <asyncio/io.h>

zero::async::coroutine::Task<void, std::error_code> asyncio::Reader::readExactly(const std::span<std::byte> data) {
    tl::expected<void, std::error_code> result;
    std::size_t offset = 0;

    while (offset < data.size()) {
        const auto n = co_await read(data.subspan(offset));

        if (!n) {
            result = tl::unexpected(n.error());
            break;
        }

        if (*n == 0) {
            result = tl::unexpected<std::error_code>(IOError::UNEXPECTED_EOF);
            break;
        }

        offset += *n;
    }

    co_return result;
}

zero::async::coroutine::Task<std::vector<std::byte>, std::error_code> asyncio::Reader::readAll() {
    tl::expected<std::vector<std::byte>, std::error_code> result;

    while (true) {
        std::byte data[10240];
        const auto n = co_await read(data);

        if (!n) {
            result = tl::unexpected(n.error());
            break;
        }

        if (*n == 0)
            break;

        std::copy_n(data, *n, std::back_inserter(*result));
    }

    co_return result;
}

zero::async::coroutine::Task<void, std::error_code> asyncio::Writer::writeAll(const std::span<const std::byte> data) {
    tl::expected<void, std::error_code> result;
    std::size_t offset = 0;

    while (offset < data.size()) {
        if (co_await zero::async::coroutine::cancelled) {
            result = tl::unexpected<std::error_code>(zero::async::coroutine::Error::CANCELLED);
            break;
        }

        const auto n = co_await write(data.subspan(offset));

        if (!n) {
            result = tl::unexpected(n.error());
            break;
        }

        assert(*n != 0);
        offset += *n;
    }

    co_return result;
}

zero::async::coroutine::Task<void, std::error_code> asyncio::copy(IReader &reader, IWriter &writer) {
    tl::expected<void, std::error_code> result;

    while (true) {
        if (co_await zero::async::coroutine::cancelled) {
            result = tl::unexpected<std::error_code>(zero::async::coroutine::Error::CANCELLED);
            break;
        }

        std::byte data[10240];
        const auto n = co_await reader.read(data);

        if (!n) {
            result = tl::unexpected(n.error());
            break;
        }

        if (*n == 0)
            break;

        co_await zero::async::coroutine::lock;

        if (const auto res = co_await writer.writeAll({data, *n}); !res) {
            result = tl::unexpected(res.error());
            break;
        }

        co_await zero::async::coroutine::unlock;
    }

    co_return result;
}

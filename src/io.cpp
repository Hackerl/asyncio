#include <asyncio/io.h>
#include <asyncio/error.h>

zero::async::coroutine::Task<void, std::error_code> asyncio::Reader::readExactly(const std::span<std::byte> data) {
    tl::expected<void, std::error_code> result;
    std::size_t offset = 0;

    while (offset < data.size()) {
        const auto n = co_await read(data.subspan(offset));

        if (!n) {
            result = tl::unexpected(n.error());
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
            if (n.error() == IO_EOF)
                break;

            result = tl::unexpected(n.error());
            break;
        }

        std::copy_n(data, *n, std::back_inserter(*result));
    }

    co_return result;
}

zero::async::coroutine::Task<void, std::error_code> asyncio::Writer::writeAll(const std::span<const std::byte> data) {
    tl::expected<void, std::error_code> result;
    std::size_t offset = 0;

    while (offset < data.size()) {
        if (CO_CANCELLED()) {
            result = tl::unexpected(make_error_code(std::errc::operation_canceled));
            break;
        }

        const auto n = co_await write(data.subspan(offset));

        if (!n) {
            result = tl::unexpected(n.error());
            break;
        }

        offset += *n;
    }

    co_return result;
}

zero::async::coroutine::Task<void, std::error_code> asyncio::copy(IReader &reader, IWriter &writer) {
    tl::expected<void, std::error_code> result;

    while (true) {
        if (CO_CANCELLED()) {
            result = tl::unexpected(make_error_code(std::errc::operation_canceled));
            break;
        }

        std::byte data[10240];
        const auto n = co_await reader.read(data);

        if (!n) {
            if (n.error() == IO_EOF)
                break;

            result = tl::unexpected(n.error());
            break;
        }

        if (const auto res = co_await writer.writeAll({data, *n}); !res) {
            result = tl::unexpected(res.error());
            break;
        }
    }

    co_return result;
}

#include <asyncio/io.h>

zero::async::coroutine::Task<void, std::error_code>
asyncio::copy(std::shared_ptr<IReader> reader, std::shared_ptr<IWriter> writer)  {
    tl::expected<void, std::error_code> result;

    while (true) {
        std::byte data[10240];
        auto n = co_await reader->read(data);

        if (!n) {
            if (n.error() == Error::IO_EOF)
                break;

            result = tl::unexpected(n.error());
            break;
        }

        auto res = co_await writer->write({data, *n});

        if (!res) {
            result = tl::unexpected(res.error());
            break;
        }
    }

    co_return result;
}

zero::async::coroutine::Task<std::vector<std::byte>, std::error_code>
asyncio::readAll(std::shared_ptr<IReader> reader) {
    tl::expected<std::vector<std::byte>, std::error_code> result;

    while (true) {
        std::byte data[10240];
        auto n = co_await reader->read(data);

        if (!n) {
            if (n.error() == Error::IO_EOF)
                break;

            result = tl::unexpected(n.error());
            break;
        }

        result->insert(result->end(), data, data + *n);
    }

    co_return result;
}

zero::async::coroutine::Task<void, std::error_code>
asyncio::readExactly(std::shared_ptr<IReader> reader, std::span<std::byte> data) {
    tl::expected<void, std::error_code> result;

    size_t offset = 0;

    while (offset < data.size()) {
        auto n = co_await reader->read(data.subspan(offset));

        if (!n) {
            result = tl::unexpected(n.error());
            break;
        }

        offset += *n;
    }

    co_return result;
}

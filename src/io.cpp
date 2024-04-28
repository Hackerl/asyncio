#include <asyncio/io.h>
#include <zero/singleton.h>

const char *asyncio::IOErrorCategory::name() const noexcept {
    return "asyncio::io";
}

std::string asyncio::IOErrorCategory::message(const int value) const {
    std::string msg;

    switch (value) {
    case BROKEN_PIPE:
        msg = "broken pipe";
        break;

    case INVALID_ARGUMENT:
        msg = "invalid argument";
        break;

    case DEVICE_OR_RESOURCE_BUSY:
        msg = "device or resource busy";
        break;

    case TIMED_OUT:
        msg = "timed out";
        break;

    case UNEXPECTED_EOF:
        msg = "unexpected end of file";
        break;

    case NOT_SUPPORTED:
        msg = "not supported";
        break;

    case OPERATION_NOT_SUPPORTED:
        msg = "operation not supported";
        break;

    case FUNCTION_NOT_SUPPORTED:
        msg = "function not supported";
        break;

    case BAD_FILE_DESCRIPTOR:
        msg = "bad file descriptor";
        break;

    case NOT_ENOUGH_MEMORY:
        msg = "not enough memory";
        break;

    case ADDRESS_FAMILY_NOT_SUPPORTED:
        msg = "address family not supported";
        break;

    default:
        msg = "unknown";
        break;
    }

    return msg;
}

std::error_condition asyncio::IOErrorCategory::default_error_condition(const int value) const noexcept {
    std::error_condition condition;

    switch (value) {
    case BROKEN_PIPE:
        condition = std::errc::broken_pipe;
        break;

    case INVALID_ARGUMENT:
        condition = std::errc::invalid_argument;
        break;

    case DEVICE_OR_RESOURCE_BUSY:
        condition = std::errc::device_or_resource_busy;
        break;

    case TIMED_OUT:
        condition = std::errc::timed_out;
        break;

    case NOT_SUPPORTED:
        condition = std::errc::not_supported;
        break;

    case OPERATION_NOT_SUPPORTED:
        condition = std::errc::operation_not_supported;
        break;

    case FUNCTION_NOT_SUPPORTED:
        condition = std::errc::function_not_supported;
        break;

    case BAD_FILE_DESCRIPTOR:
        condition = std::errc::bad_file_descriptor;
        break;

    case NOT_ENOUGH_MEMORY:
        condition = std::errc::not_enough_memory;
        break;

    case ADDRESS_FAMILY_NOT_SUPPORTED:
        condition = std::errc::address_family_not_supported;
        break;

    default:
        condition = error_category::default_error_condition(value);
        break;
    }

    return condition;
}

std::error_code asyncio::make_error_code(const IOError e) {
    return {e, zero::Singleton<IOErrorCategory>::getInstance()};
}

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
            result = tl::unexpected<std::error_code>(UNEXPECTED_EOF);
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

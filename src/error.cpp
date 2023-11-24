#include <asyncio/error.h>

const char *asyncio::ErrorCategory::name() const noexcept {
    return "asyncio";
}

std::string asyncio::ErrorCategory::message(const int value) const {
    std::string msg;

    switch (value) {
    case IO_EOF:
        msg = "eof";
        break;

    case RESOURCE_DESTROYED:
        msg = "resource destroyed";
        break;

    default:
        msg = "unknown";
        break;
    }

    return msg;
}

const std::error_category &asyncio::errorCategory() {
    static ErrorCategory instance;
    return instance;
}

std::error_code asyncio::make_error_code(const Error e) {
    return {static_cast<int>(e), errorCategory()};
}

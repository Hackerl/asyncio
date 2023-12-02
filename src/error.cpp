#include <asyncio/error.h>

const char *asyncio::ErrorCategory::name() const noexcept {
    return "asyncio";
}

std::string asyncio::ErrorCategory::message(const int value) const {
    if (value == IO_EOF)
        return "eof";

    return "unknown";
}

const std::error_category &asyncio::errorCategory() {
    static ErrorCategory instance;
    return instance;
}

std::error_code asyncio::make_error_code(const Error e) {
    return {static_cast<int>(e), errorCategory()};
}

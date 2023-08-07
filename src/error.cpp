#include <asyncio/error.h>

const char *asyncio::Category::name() const noexcept {
    return "asyncio";
}

std::string asyncio::Category::message(int value) const {
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

const std::error_category &asyncio::asyncio_category() {
    static Category instance;
    return instance;
}

std::error_code asyncio::make_error_code(asyncio::Error e) {
    return {static_cast<int>(e), asyncio_category()};
}

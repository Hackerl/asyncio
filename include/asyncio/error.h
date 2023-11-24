#ifndef ASYNCIO_ERROR_H
#define ASYNCIO_ERROR_H

#include <system_error>

namespace asyncio {
    enum Error {
        IO_EOF = 1,
        RESOURCE_DESTROYED
    };

    class ErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
    };

    const std::error_category &errorCategory();
    std::error_code make_error_code(Error e);
}

template<>
struct std::is_error_code_enum<asyncio::Error> : std::true_type {
};

#endif //ASYNCIO_ERROR_H

#ifndef ASYNCIO_ERROR_H
#define ASYNCIO_ERROR_H

#include <system_error>

namespace asyncio {
    enum Error {
        IO_EOF = 1,
        RESOURCE_DESTROYED
    };

    class Category : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
    };

    const std::error_category &category();
    std::error_code make_error_code(Error e);
}

namespace std {
    template<>
    struct is_error_code_enum<asyncio::Error> : public true_type {

    };
}

#endif //ASYNCIO_ERROR_H

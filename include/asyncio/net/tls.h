#ifndef ASYNCIO_TLS_H
#define ASYNCIO_TLS_H

#include <openssl/err.h>

namespace asyncio::net::tls {
    DEFINE_ERROR_TRANSFORMER(
        Error,
        "asyncio::net::tls",
        ([](const int value) -> std::string {
            std::array<char, 1024> buffer = {};
            ERR_error_string_n(static_cast<unsigned long>(value), buffer.data(), buffer.size());
            return buffer.data();
        })
    )

    template<typename F>
        requires std::is_same_v<std::invoke_result_t<F>, int>
    std::expected<std::invoke_result_t<F>, std::error_code> expected(F &&f) {
        const auto result = f();

        if (!result)
            return std::unexpected(static_cast<Error>(result));

        return result;
    }
}

DECLARE_ERROR_CODE(asyncio::net::tls::Error)

#endif //ASYNCIO_TLS_H

#ifndef ASYNCIO_ERROR_H
#define ASYNCIO_ERROR_H

#include "task.h"
#include <fmt/std.h>
#include <fmt/ranges.h>

namespace asyncio::error {
    class SystemError final : public std::system_error {
        SystemError(const std::error_code ec, const std::vector<std::source_location> &stacktrace)
            : std::system_error{ec},
              mMessage{
                  fmt::format(
                      "{} {}",
                      std::system_error::what(),
                      to_string(fmt::join(stacktrace | std::views::drop(1), "\n"))
                  )
              } {
        }

    public:
        template<typename... Args>
        static task::Task<SystemError> make(Args &&... args) {
            co_return SystemError{{std::forward<Args>(args)...}, co_await task::backtrace};
        }

        [[nodiscard]] const char *what() const noexcept override {
            return mMessage.c_str();
        }

    private:
        std::string mMessage;
    };

    template<typename T, typename E>
        requires std::is_convertible_v<E, std::error_code>
    task::Task<T> guard(std::expected<T, E> expected) {
        if (!expected)
            throw co_await SystemError::make(expected.error());

        if constexpr (std::is_void_v<T>)
            co_return;
        else
            co_return *std::move(expected);
    }

    template<typename T, typename E>
        requires std::is_convertible_v<E, std::error_code>
    task::Task<T> guard(task::Task<T, E> task) {
        auto result = co_await task;

        if (!result)
            throw co_await SystemError::make(result.error());

        if constexpr (std::is_void_v<T>)
            co_return;
        else
            co_return *std::move(result);
    }
}

#endif //ASYNCIO_ERROR_H

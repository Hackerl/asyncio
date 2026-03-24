#ifndef ASYNCIO_ERROR_H
#define ASYNCIO_ERROR_H

#include "task.h"
#include <fmt/std.h>
#include <fmt/ranges.h>

namespace asyncio::error {
    template<std::derived_from<std::exception> T>
    class StacktraceError final : public T {
    public:
        template<typename... Args>
        explicit StacktraceError(const std::vector<std::source_location> &stacktrace, Args &&... args)
            : T{std::forward<Args>(args)...},
              mMessage{fmt::format("{} {}", T::what(), to_string(fmt::join(stacktrace | std::views::drop(1), "\n")))} {
        }

        template<typename... Args>
        static task::Task<StacktraceError> make(Args &&... args) {
            co_return StacktraceError{co_await task::backtrace, std::forward<Args>(args)...};
        }

        [[nodiscard]] const char *what() const noexcept override {
            return mMessage.c_str();
        }

    private:
        std::string mMessage;
    };

    template<typename T, std::convertible_to<std::error_code> E>
    task::Task<T> guard(std::expected<T, E> expected) {
        if (!expected)
            throw co_await StacktraceError<std::system_error>::make(expected.error());

        if constexpr (std::is_void_v<T>)
            co_return;
        else
            co_return *std::move(expected);
    }

    template<typename T, std::convertible_to<std::error_code> E>
    task::Task<T> guard(task::Task<T, E> task) {
        auto result = co_await task;

        if (!result)
            throw co_await StacktraceError<std::system_error>::make(result.error());

        if constexpr (std::is_void_v<T>)
            co_return;
        else
            co_return *std::move(result);
    }

    template<typename T>
    task::Task<std::expected<T, std::exception_ptr>>
    capture(task::Task<T> task) {
        try {
            if constexpr (std::is_void_v<T>) {
                co_await task;
                co_return {};
            }
            else {
                co_return co_await task;
            }
        }
        catch (const std::exception &) {
            co_return std::unexpected{std::current_exception()};
        }
    }
}

#endif //ASYNCIO_ERROR_H

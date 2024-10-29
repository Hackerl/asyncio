#ifndef ASYNCIO_CATCH_EXTENSIONS_H
#define ASYNCIO_CATCH_EXTENSIONS_H

#define CATCH_CONFIG_ENABLE_ALL_STRINGMAKERS

#include <fmt/std.h>
#include <zero/formatter.h>
#include <asyncio/task.h>
#include <catch2/catch_test_macros.hpp>

#define INTERNAL_ASYNC_TEST_CASE(func, ...)         \
    static asyncio::task::Task<void> func();        \
                                                    \
    TEST_CASE(__VA_ARGS__) {                        \
        const auto _result = asyncio::run(func);    \
        REQUIRE(_result);                           \
        REQUIRE(*_result);                          \
    }                                               \
                                                    \
    asyncio::task::Task<void> func()

#define ASYNC_TEST_CASE(...) INTERNAL_ASYNC_TEST_CASE(INTERNAL_CATCH_UNIQUE_NAME(CATCH2_INTERNAL_TEST_ASYNC_), __VA_ARGS__)

template<>
struct Catch::StringMaker<std::exception_ptr> {
    static std::string convert(const std::exception_ptr &ptr) {
        return fmt::to_string(ptr);
    }
};

template<typename T, typename E>
struct Catch::StringMaker<std::expected<T, E>> {
    static std::string convert(const std::expected<T, E> &expected) {
        if (!expected)
            return fmt::format("unexpected({})", StringMaker<E>::convert(expected.error()));

        if constexpr (std::is_void_v<T>)
            return "expected()";
        else
            return fmt::format("expected({})", StringMaker<T>::convert(*expected));
    }
};

template<>
struct Catch::StringMaker<std::error_code> {
    static std::string convert(const std::error_code &ec) {
        return fmt::format("{} ({})", ec.message(), ec);
    }
};

template<typename T>
    requires std::is_error_code_enum_v<T>
struct Catch::StringMaker<T> {
    static std::string convert(const T &error) {
        return StringMaker<std::error_code>::convert(error);
    }
};

template<>
struct Catch::StringMaker<std::error_condition> {
    static std::string convert(const std::error_condition &condition) {
        return fmt::format("{} ({}:{})", condition.message(), condition.category().name(), condition.value());
    }
};

template<typename T>
    requires std::is_error_condition_enum_v<T>
struct Catch::StringMaker<T> {
    static std::string convert(const T &error) {
        return StringMaker<std::error_condition>::convert(error);
    }
};

#define INTERNAL_REQUIRE_ERROR(var, expr, err)  \
    const auto var = expr;                      \
    REQUIRE_FALSE(var);                         \
    REQUIRE((var).error() == (err))

#define REQUIRE_ERROR(expr, err) INTERNAL_REQUIRE_ERROR(INTERNAL_CATCH_UNIQUE_NAME(_result), expr, err)

#endif //ASYNCIO_CATCH_EXTENSIONS_H

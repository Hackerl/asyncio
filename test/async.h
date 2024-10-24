#ifndef ASYNCIO_TEST_ASYNC_H
#define ASYNCIO_TEST_ASYNC_H

#include <asyncio/task.h>
#include <catch2/catch_test_macros.hpp>

#define INTERNAL_ASYNC_TEST_CASE(func, ...)         \
    asyncio::task::Task<void> func();               \
                                                    \
    TEST_CASE(__VA_ARGS__) {                        \
        const auto _result = asyncio::run(func);    \
        REQUIRE(_result);                           \
        REQUIRE(*_result);                          \
    }                                               \
                                                    \
    asyncio::task::Task<void> func()

#define ASYNC_TEST_CASE(...) INTERNAL_ASYNC_TEST_CASE(INTERNAL_CATCH_UNIQUE_NAME(CATCH2_INTERNAL_TEST_ASYNC_), __VA_ARGS__)

#endif //ASYNCIO_TEST_ASYNC_H

#ifndef ASYNCIO_CONCEPTS_H
#define ASYNCIO_CONCEPTS_H

#include <zero/meta/concepts.h>

namespace asyncio {
    namespace task {
        template<typename T, typename E>
        class Task;
    }

    template<typename F, typename... Args>
    concept Invocable = requires(F &&f, Args &&... args) {
        { std::invoke(std::forward<F>(f), std::forward<Args>(args)...) } -> zero::meta::Specialization<task::Task>;
    };
}

#endif //ASYNCIO_CONCEPTS_H

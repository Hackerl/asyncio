#ifndef ASYNCIO_EVENT_LOOP_H
#define ASYNCIO_EVENT_LOOP_H

#include "uv.h"
#include <mutex>
#include <queue>
#include <cassert>
#include <zero/detail/type_traits.h>

namespace asyncio {
    class EventLoop {
        struct TaskQueue {
            uv::Handle<uv_async_t> async;
            std::mutex mutex;
            std::queue<std::function<void()>> queue;
        };

    public:
        explicit EventLoop(
            std::unique_ptr<uv_loop_t, void (*)(uv_loop_t *)> loop,
            std::unique_ptr<TaskQueue> taskQueue
        );

        EventLoop(EventLoop &&rhs) = default;
        EventLoop &operator=(EventLoop &&rhs) noexcept = default;

        ~EventLoop();

        static EventLoop make();

        uv_loop_t *raw();
        [[nodiscard]] const uv_loop_t *raw() const;

        void post(std::function<void()> function);

        void stop();
        void run();

    private:
        std::unique_ptr<uv_loop_t, void (*)(uv_loop_t *)> mLoop;
        std::unique_ptr<TaskQueue> mTaskQueue;
    };

    std::shared_ptr<EventLoop> getEventLoop();
    void setEventLoop(const std::weak_ptr<EventLoop> &eventLoop);

    namespace task {
        template<typename T, typename E>
        class Task;
    }

    template<typename F>
        requires zero::detail::is_specialization_v<std::invoke_result_t<F>, task::Task>
    std::expected<
        typename std::invoke_result_t<F>::value_type,
        typename std::invoke_result_t<F>::error_type
    >
    run(const std::shared_ptr<EventLoop> &eventLoop, F &&f) {
        setEventLoop(eventLoop);

        auto task = f().addCallback([&] {
            eventLoop->stop();
        });

        eventLoop->run();
        assert(task.done());
        return {task.future().result()};
    }

    template<typename F>
        requires zero::detail::is_specialization_v<std::invoke_result_t<F>, task::Task>
    auto run(F &&f) {
        return run(std::make_shared<EventLoop>(EventLoop::make()), std::forward<F>(f));
    }

    task::Task<void, std::error_code> reschedule();
}

#endif //ASYNCIO_EVENT_LOOP_H

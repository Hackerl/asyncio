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

        static std::expected<EventLoop, std::error_code> make();

        uv_loop_t *raw();
        [[nodiscard]] const uv_loop_t *raw() const;

        std::expected<void, std::error_code> post(std::function<void()> function);

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

    template<typename F, typename T = std::invoke_result_t<F>>
        requires zero::detail::is_specialization_v<T, task::Task>
    std::expected<std::expected<typename T::value_type, typename T::error_type>, std::error_code> run(F &&f) {
        const auto eventLoop = EventLoop::make().transform([](EventLoop &&value) {
            return std::make_shared<EventLoop>(std::move(value));
        });
        EXPECT(eventLoop);

        setEventLoop(*eventLoop);

        auto future = f().future().finally([&] {
            eventLoop.value()->stop();
        });

        eventLoop.value()->run();
        assert(future.isReady());
        return {std::move(future).result()};
    }
}

#endif //ASYNCIO_EVENT_LOOP_H

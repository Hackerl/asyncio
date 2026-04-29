#ifndef ASYNCIO_EVENT_LOOP_H
#define ASYNCIO_EVENT_LOOP_H

#include "uv.h"
#include "promise.h"
#include "concepts.h"
#include <mutex>
#include <queue>
#include <cassert>

namespace asyncio {
    class EventLoop final : public zero::async::promise::IExecutor {
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

        ~EventLoop() override;

        static std::shared_ptr<EventLoop> make();

        uv_loop_t *raw();
        [[nodiscard]] const uv_loop_t *raw() const;

        void post(std::function<void()> f) override;

        template<typename F>
            requires (std::invocable<F> && !Invocable<F>)
        SemiFuture<std::invoke_result_t<F>> submit(F &&f) {
            using T = std::invoke_result_t<F>;

            auto promise = std::make_shared<Promise<T>>();
            auto future = promise->getFuture();

            post([promise = std::move(promise), f = std::forward<F>(f)] mutable {
                auto result = zero::error::capture(std::move(f));

                if (!result) {
                    promise->reject(std::move(result).error());
                    return;
                }

                if constexpr (std::is_void_v<T>)
                    promise->resolve();
                else
                    promise->resolve(*std::move(result));
            });

            return future;
        }

        template<Invocable F>
        SemiFuture<
            typename std::invoke_result_t<F>::value_type,
            typename std::invoke_result_t<F>::error_type
        > submit(F &&f) {
            using T = std::invoke_result_t<F>::value_type;
            using E = std::invoke_result_t<F>::error_type;

            auto promise = std::make_shared<Promise<T, E>>();
            auto future = promise->getFuture();

            post([promise = std::move(promise), f = std::forward<F>(f)] mutable {
                std::invoke(std::move(f))
                    .future()
                    .then([=]<typename... Args>(Args &&... args) {
                        promise->resolve(std::forward<Args>(args)...);
                    }).fail([=](E &&error) {
                        promise->reject(std::move(error));
                    });
            });

            return future;
        }

        void stop();
        void run();

    private:
        std::unique_ptr<uv_loop_t, void (*)(uv_loop_t *)> mLoop;
        std::unique_ptr<TaskQueue> mTaskQueue;
    };

    std::shared_ptr<EventLoop> getEventLoop();
    void setEventLoop(const std::weak_ptr<EventLoop> &eventLoop);

    template<Invocable F>
    std::expected<
        typename std::invoke_result_t<F>::value_type,
        typename std::invoke_result_t<F>::error_type
    >
    run(const std::shared_ptr<EventLoop> &eventLoop, F &&f) {
        setEventLoop(eventLoop);

        auto task = std::invoke(std::forward<F>(f)).addCallback([&] {
            eventLoop->stop();
        });

        eventLoop->run();
        assert(task.done());
        return {task.future().result()};
    }

    template<Invocable F>
    auto run(F &&f) {
        return run(EventLoop::make(), std::forward<F>(f));
    }

    task::Task<void, std::error_code> reschedule();
}

#endif //ASYNCIO_EVENT_LOOP_H

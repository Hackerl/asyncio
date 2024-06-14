#include <asyncio/event_loop.h>

thread_local std::weak_ptr<asyncio::EventLoop> threadEventLoop;

asyncio::EventLoop::EventLoop(
    std::unique_ptr<uv_loop_t, void(*)(uv_loop_t *)> loop,
    std::unique_ptr<TaskQueue> taskQueue
): mLoop(std::move(loop)), mTaskQueue(std::move(taskQueue)) {
}

uv_loop_t *asyncio::EventLoop::raw() {
    return mLoop.get();
}

const uv_loop_t *asyncio::EventLoop::raw() const {
    return mLoop.get();
}

std::expected<asyncio::EventLoop, std::error_code> asyncio::EventLoop::make() {
    auto loop = std::make_unique<uv_loop_t>();

    EXPECT(uv::expected([&] {
        return uv_loop_init(loop.get());
    }));

    auto async = std::make_unique<uv_async_t>();

    EXPECT(uv::expected([&] {
        return uv_async_init(
            loop.get(),
            async.get(),
            [](const auto handle) {
                auto &[async, mutex, queue] = *static_cast<TaskQueue *>(handle->data);

                while (true) {
                    mutex.lock();

                    if (queue.empty()) {
                        mutex.unlock();
                        break;
                    }

                    const auto function = std::move(queue.front());
                    queue.pop();
                    mutex.unlock();

                    function();
                }
            }
        );
    }));

    auto taskQueue = std::make_unique<TaskQueue>(uv::Handle{std::move(async)});
    taskQueue->async->data = taskQueue.get();

    return EventLoop{
        {
            loop.release(),
            [](uv_loop_t *ptr) {
                uv_loop_close(ptr);
                delete ptr;
            }
        },
        std::move(taskQueue)
    };
}

// ReSharper disable once CppMemberFunctionMayBeConst
std::expected<void, std::error_code> asyncio::EventLoop::post(std::function<void()> function) {
    std::lock_guard guard(mTaskQueue->mutex);
    mTaskQueue->queue.push(std::move(function));

    EXPECT(uv::expected([this] {
        return uv_async_send(mTaskQueue->async.raw());
    }));

    return {};
}

// ReSharper disable once CppMemberFunctionMayBeConst
void asyncio::EventLoop::stop() {
    uv_stop(mLoop.get());
}

// ReSharper disable once CppMemberFunctionMayBeConst
std::expected<void, std::error_code> asyncio::EventLoop::run() {
    EXPECT(uv::expected([this] {
        return uv_run(mLoop.get(), UV_RUN_DEFAULT);
    }));
    return {};
}

std::shared_ptr<asyncio::EventLoop> asyncio::getEventLoop() {
    if (threadEventLoop.expired())
        return nullptr;

    return threadEventLoop.lock();
}

void asyncio::setEventLoop(const std::weak_ptr<EventLoop> &eventLoop) {
    threadEventLoop = eventLoop;
}

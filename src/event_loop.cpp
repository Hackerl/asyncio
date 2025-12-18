#include <asyncio/event_loop.h>
#include <asyncio/task.h>

thread_local std::weak_ptr<asyncio::EventLoop> threadEventLoop;

asyncio::EventLoop::EventLoop(
    std::unique_ptr<uv_loop_t, void(*)(uv_loop_t *)> loop,
    std::unique_ptr<TaskQueue> taskQueue
): mLoop{std::move(loop)}, mTaskQueue{std::move(taskQueue)} {
}

asyncio::EventLoop::~EventLoop() {
    if (!mLoop)
        return;

    mTaskQueue.reset();

    while (true) {
        if (uv_run(mLoop.get(), UV_RUN_NOWAIT) == 0)
            break;
    }
}

uv_loop_t *asyncio::EventLoop::raw() {
    return mLoop.get();
}

const uv_loop_t *asyncio::EventLoop::raw() const {
    return mLoop.get();
}

asyncio::EventLoop asyncio::EventLoop::make() {
    auto loop = std::make_unique<uv_loop_t>();

    zero::error::guard(uv::expected([&] {
        return uv_loop_init(loop.get());
    }));

    auto async = std::make_unique<uv_async_t>();

    zero::error::guard(uv::expected([&] {
        return uv_async_init(
            loop.get(),
            async.get(),
            [](auto *handle) {
                auto &[h, mutex, queue] = *static_cast<TaskQueue *>(handle->data);

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
                zero::error::guard(uv::expected([&] {
                    return uv_loop_close(ptr);
                }));
                delete ptr;
            }
        },
        std::move(taskQueue)
    };
}

// ReSharper disable once CppMemberFunctionMayBeConst
std::expected<void, std::error_code> asyncio::EventLoop::post(std::function<void()> function) {
    const std::lock_guard guard{mTaskQueue->mutex};
    mTaskQueue->queue.push(std::move(function));

    Z_EXPECT(uv::expected([this] {
        return uv_async_send(mTaskQueue->async.raw());
    }));

    return {};
}

// ReSharper disable once CppMemberFunctionMayBeConst
void asyncio::EventLoop::stop() {
    uv_stop(mLoop.get());
}

// ReSharper disable once CppMemberFunctionMayBeConst
void asyncio::EventLoop::run() {
    zero::error::guard(uv::expected([this] {
        return uv_run(mLoop.get(), UV_RUN_DEFAULT);
    }));
}

std::shared_ptr<asyncio::EventLoop> asyncio::getEventLoop() {
    if (threadEventLoop.expired())
        return nullptr;

    return threadEventLoop.lock();
}

void asyncio::setEventLoop(const std::weak_ptr<EventLoop> &eventLoop) {
    threadEventLoop = eventLoop;
}

asyncio::task::Task<void, std::error_code> asyncio::reschedule() {
    auto ptr = std::make_unique<uv_idle_t>();

    Z_CO_EXPECT(uv::expected([&] {
        return uv_idle_init(getEventLoop()->raw(), ptr.get());
    }));

    uv::Handle idle{std::move(ptr)};

    Promise<void, std::error_code> promise;
    idle->data = &promise;

    Z_CO_EXPECT(uv::expected([&] {
        return uv_idle_start(
            idle.raw(),
            [](auto *handle) {
                zero::error::guard(uv::expected([&] {
                    return uv_idle_stop(handle);
                }));
                static_cast<Promise<void, std::error_code> *>(handle->data)->resolve();
            }
        );
    }));

    co_return co_await task::CancellableFuture{
        promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            if (promise.isFulfilled())
                return std::unexpected{task::Error::WILL_BE_DONE};

            zero::error::guard(uv::expected([&] {
                return uv_idle_stop(idle.raw());
            }));

            promise.reject(task::Error::CANCELLED);
            return {};
        }
    };
}

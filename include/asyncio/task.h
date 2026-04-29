#ifndef ASYNCIO_TASK_H
#define ASYNCIO_TASK_H

#include "event_loop.h"
#include <list>
#include <algorithm>
#include <coroutine>
#include <exception>
#include <source_location>
#include <treehh/tree.hh>

namespace asyncio::task {
    Z_DEFINE_ERROR_CODE_EX(
        Error,
        "asyncio::task",
        Cancelled, "Task was cancelled", std::errc::operation_canceled,
        CancellationNotSupported, "Task does not support cancellation", std::errc::operation_not_supported,
        Locked, "Task is locked", std::errc::resource_unavailable_try_again,
        CancellationTooLate, "Cancellation is too late", std::errc::operation_not_permitted,
        AlreadyCompleted, "Task is already completed", std::errc::operation_not_permitted
    )

    template<typename T, typename E = std::exception_ptr>
    struct Awaitable {
        [[nodiscard]] bool await_ready() {
            if (!future.isReady())
                return false;

            if (onReady)
                std::exchange(onReady, nullptr)();

            result.emplace(std::move(future).result());
            return true;
        }

        void await_suspend(const std::coroutine_handle<> handle) {
            future.setCallback([=, this](std::expected<T, E> &&res) {
                if (onReady)
                    std::exchange(onReady, nullptr)();

                if (!res) {
                    result.emplace(std::unexpected{std::move(res).error()});
                    handle.resume();
                    return;
                }

                if constexpr (std::is_void_v<T>)
                    result.emplace();
                else
                    result.emplace(*std::move(res));

                handle.resume();
            });
        }

        std::expected<T, E> await_resume() requires (!std::same_as<E, std::exception_ptr>) {
            return std::move(*result);
        }

        T await_resume() requires std::same_as<E, std::exception_ptr> {
            if (!result->has_value())
                std::rethrow_exception(result->error());

            if constexpr (!std::is_void_v<T>)
                return *std::move(*result);
            else
                return;
        }

        Future<T, E> future;
        std::function<void()> onReady;
        std::optional<std::expected<T, E>> result;
    };

    template<typename T, typename E>
    class Promise;

    class TaskGroup;

    struct Frame {
        std::weak_ptr<Frame> parent;
        std::list<std::shared_ptr<Frame>> children;
        std::optional<std::source_location> location;
        std::function<std::expected<void, std::error_code>()> cancel;
        std::list<std::function<void()>> callbacks;
        std::shared_ptr<EventLoop> eventLoop{getEventLoop()};
        bool finished{false};
        bool locked{false};
        bool cancelled{false};

        void step();
        void end();
        std::expected<void, std::error_code> cancelAll();

        [[nodiscard]] tree<std::source_location> callTree() const;
        [[nodiscard]] std::string trace() const;
    };

    template<typename T>
        requires (
            zero::meta::Specialization<T, SemiFuture> ||
            zero::meta::Specialization<T, Future> ||
            zero::meta::Specialization<T, Task>
        )
    struct Cancellable {
        T awaitable;
        std::function<std::expected<void, std::error_code>()> cancel;
    };

    struct Cancelled {
    };

    struct Lock {
    };

    struct Unlock {
    };

    struct Backtrace {
    };

    inline constexpr Cancelled cancelled;
    inline constexpr Lock lock;
    inline constexpr Unlock unlock;
    inline constexpr Backtrace backtrace;

    template<typename F, typename T>
    using InvokeResult = std::conditional_t<
        std::is_void_v<T>,
        std::invoke_result<F>,
        std::invoke_result<F, T>
    >::type;

    template<typename F, typename T>
    concept AsyncFunction =
        (!std::is_void_v<T> && requires(F &&f, T &&arg) {
            { std::invoke(std::forward<F>(f), std::forward<T>(arg)) } -> zero::meta::Specialization<Task>;
        }) ||
        (std::is_void_v<T> && requires(F &&f) {
            { std::invoke(std::forward<F>(f)) } -> zero::meta::Specialization<Task>;
        });

    template<typename F, typename T>
    concept FallibleFunction =
        (!std::is_void_v<T> && requires(F &&f, T &&arg) {
            { std::invoke(std::forward<F>(f), std::forward<T>(arg)) } -> zero::meta::Specialization<std::expected>;
        }) ||
        (std::is_void_v<T> && requires(F &&f) {
            { std::invoke(std::forward<F>(f)) } -> zero::meta::Specialization<std::expected>;
        });

    template<typename T, typename E = std::exception_ptr>
    class Task {
    public:
        using value_type = T;
        using error_type = E;
        using promise_type = Promise<T, E>;

        Task(std::shared_ptr<Frame> frame, std::shared_ptr<asyncio::Promise<T, E>> promise)
            : mFrame{std::move(frame)}, mPromise{std::move(promise)} {
        }

        Task(Task &&rhs) noexcept : mFrame{std::move(rhs.mFrame)}, mPromise{std::move(rhs.mPromise)} {
        }

        Task &operator=(Task &&rhs) noexcept {
            mFrame = std::move(rhs.mFrame);
            mPromise = std::move(rhs.mPromise);
            return *this;
        }

        // ReSharper disable once CppMemberFunctionMayBeConst
        std::expected<void, std::error_code> cancel() {
            return mFrame->cancelAll();
        }

        [[nodiscard]] tree<std::source_location> callTree() const {
            return mFrame->callTree();
        }

        [[nodiscard]] std::string trace() const {
            return mFrame->trace();
        }

        template<zero::meta::Mutable Self>
        Self &&addCallback(this Self &&self, std::function<void()> callback) {
            if (self.done()) {
                self.mFrame->eventLoop->post([callback = std::move(callback)] {
                    callback();
                });

                return std::forward<Self>(self);
            }

            self.mFrame->callbacks.push_back(std::move(callback));
            return std::forward<Self>(self);
        }

        template<AsyncFunction<T> F>
        Task<typename InvokeResult<F, T>::value_type, E> transform(F f) &&
            requires (!std::same_as<E, std::exception_ptr>) {
            static_assert(std::is_same_v<typename InvokeResult<F, T>::error_type, std::exception_ptr>);

            auto result = co_await *this;

            if (!result)
                co_return std::unexpected{std::move(result).error()};

            if constexpr (std::is_void_v<T>)
                co_return co_await std::invoke(std::move(f));
            else
                co_return co_await std::invoke(std::move(f), *std::move(result));
        }

        template<typename F>
            requires (!AsyncFunction<F, T>)
        Task<InvokeResult<F, T>, E> transform(F f) && requires (!std::same_as<E, std::exception_ptr>) {
            co_return (co_await *this).transform(std::move(f));
        }

        template<AsyncFunction<T> F>
        Task<typename InvokeResult<F, T>::value_type, E> andThen(F f) &&
            requires (!std::same_as<E, std::exception_ptr>) {
            static_assert(std::is_same_v<typename InvokeResult<F, T>::error_type, E>);

            auto result = co_await *this;

            if (!result)
                co_return std::unexpected{std::move(result).error()};

            if constexpr (std::is_void_v<T>)
                co_return co_await std::invoke(std::move(f));
            else
                co_return co_await std::invoke(std::move(f), *std::move(result));
        }

        template<FallibleFunction<T> F>
        Task<typename InvokeResult<F, T>::value_type, E> andThen(F f) &&
            requires (!std::same_as<E, std::exception_ptr>) {
            co_return (co_await *this).and_then(std::move(f));
        }

        template<AsyncFunction<E> F>
        Task<T, typename InvokeResult<F, E>::value_type> transformError(F f) &&
            requires (!std::same_as<E, std::exception_ptr>) {
            static_assert(std::is_same_v<typename InvokeResult<F, E>::error_type, std::exception_ptr>);
            auto result = co_await *this;

            if (!result)
                co_return std::unexpected{co_await std::invoke(std::move(f), std::move(result).error())};

            if constexpr (std::is_void_v<T>)
                co_return {};
            else
                co_return *std::move(result);
        }

        template<typename F>
            requires (!AsyncFunction<F, E>)
        Task<T, InvokeResult<F, E>> transformError(F f) && requires (!std::same_as<E, std::exception_ptr>) {
            co_return (co_await *this).transform_error(std::move(f));
        }

        template<AsyncFunction<E> F>
        Task<T, typename InvokeResult<F, E>::error_type> orElse(F f) &&
            requires (!std::same_as<E, std::exception_ptr>) {
            static_assert(std::is_same_v<typename InvokeResult<F, E>::value_type, T>);
            auto result = co_await *this;

            if (!result)
                co_return co_await std::invoke(std::move(f), std::move(result).error());

            if constexpr (std::is_void_v<T>)
                co_return {};
            else
                co_return *std::move(result);
        }

        template<FallibleFunction<E> F>
        Task<T, typename InvokeResult<F, E>::error_type> orElse(F f) &&
            requires (!std::same_as<E, std::exception_ptr>) {
            co_return (co_await *this).or_else(std::move(f));
        }

        [[nodiscard]] bool done() const {
            return mFrame->finished;
        }

        [[nodiscard]] bool cancelled() const {
            return mFrame->cancelled;
        }

        [[nodiscard]] bool locked() const {
            return mFrame->locked;
        }

        Future<T, E> future() {
            return mPromise->getFuture().via(mFrame->eventLoop);
        }

    private:
        std::shared_ptr<Frame> mFrame;
        std::shared_ptr<asyncio::Promise<T, E>> mPromise;

        template<typename, typename>
        friend class PromiseBase;

        friend class TaskGroup;
    };

    class TaskGroup {
    public:
        [[nodiscard]] bool cancelled() const;

        std::expected<void, std::error_code> cancel();

        template<typename T>
            requires zero::meta::Specialization<std::remove_cvref_t<T>, Task>
        void add(T &&task) {
            if (task.mFrame->finished)
                return;

            if (mCancelled)
                std::ignore = task.cancel();

            auto frame = task.mFrame;
            mFrames.push_back(frame);

            // Although a circular reference is created, it will be broken after the task is completed.
            task.addCallback([frame = std::move(frame), this] {
                mFrames.remove(frame);
            });
        }

    private:
        bool mCancelled{false};
        std::list<std::shared_ptr<Frame>> mFrames;

        template<typename T, typename E>
        friend class PromiseBase;

        friend struct Frame;
    };

    template<typename T, typename E>
    class PromiseBase {
    public:
        PromiseBase() : mFrame{std::make_shared<Frame>()}, mPromise{std::make_shared<asyncio::Promise<T, E>>()} {
        }

        std::suspend_never initial_suspend() {
            return {};
        }

        std::suspend_never final_suspend() noexcept {
            return {};
        }

        void unhandled_exception() {
            mFrame->end();

            if constexpr (std::is_same_v<E, std::exception_ptr>)
                mPromise->reject(std::current_exception());
            else
                std::rethrow_exception(std::current_exception());
        }

        Task<T, E> get_return_object() {
            return {mFrame, mPromise};
        }

        [[nodiscard]] Awaitable<bool> await_transform(const Cancelled) const {
            return {Future<bool>::resolved(mFrame->cancelled)};
        }

        [[nodiscard]] Awaitable<void> await_transform(const Lock) const {
            mFrame->locked = true;
            return {Future<void>::resolved()};
        }

        [[nodiscard]] Awaitable<void> await_transform(const Unlock) const {
            assert(mFrame->locked);
            mFrame->locked = false;
            return {Future<void>::resolved()};
        }

        [[nodiscard]] Awaitable<std::vector<std::source_location>>
        await_transform(const Backtrace, const std::source_location location = std::source_location::current()) const {
            const auto &eventLoop = mFrame->eventLoop;
            auto [promise, future] = contract<std::vector<std::source_location>>(eventLoop);

            eventLoop->post(
                [
                    =,
                    this,
                    promise = std::make_shared<asyncio::Promise<std::vector<std::source_location>>>(
                        std::move(promise)
                    )
                ] {
                    std::vector stacktrace{location};

                    auto frame = mFrame->parent.lock();

                    while (frame) {
                        assert(frame->location);
                        stacktrace.push_back(*frame->location);
                        frame = frame->parent.lock();
                    }

                    promise->resolve(std::move(stacktrace));
                }
            );

            mFrame->location = location;
            return {std::move(future), [this] { mFrame->step(); }};
        }

        template<typename Value, typename Error>
        Awaitable<Value, Error>
        await_transform(
            Cancellable<SemiFuture<Value, Error>> cancellable,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = cancellable.cancel();
            else
                mFrame->cancel = std::move(cancellable.cancel);

            return {std::move(cancellable.awaitable).via(mFrame->eventLoop), [this] { mFrame->step(); }};
        }

        template<typename Value, typename Error>
        Awaitable<Value, Error>
        await_transform(
            Cancellable<Future<Value, Error>> cancellable,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = cancellable.cancel();
            else
                mFrame->cancel = std::move(cancellable.cancel);

            return {std::move(cancellable.awaitable).via(mFrame->eventLoop), [this] { mFrame->step(); }};
        }

        template<typename Value, typename Error>
        Awaitable<Value, Error>
        await_transform(
            Cancellable<Task<Value, Error>> cancellable,
            const std::source_location location = std::source_location::current()
        ) {
            cancellable.awaitable.mFrame->parent = mFrame;
            mFrame->children.push_back(cancellable.awaitable.mFrame);
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = cancellable.cancel();
            else
                mFrame->cancel = std::move(cancellable.cancel);

            return {cancellable.awaitable.future(), [this] { mFrame->step(); }};
        }

        template<typename Value, typename Error>
        Awaitable<Value, Error>
        await_transform(
            SemiFuture<Value, Error> future,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->location = location;
            return {std::move(future).via(mFrame->eventLoop), [this] { mFrame->step(); }};
        }

        template<typename Value, typename Error>
        Awaitable<Value, Error>
        await_transform(
            Future<Value, Error> future,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->location = location;
            return {std::move(future).via(mFrame->eventLoop), [this] { mFrame->step(); }};
        }

        template<typename Value, typename Error>
        Awaitable<Value, Error>
        await_transform(
            Task<Value, Error> &&task,
            const std::source_location location = std::source_location::current()
        ) {
            task.mFrame->parent = mFrame;
            mFrame->children.push_back(task.mFrame);
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = task.cancel();

            return {task.future(), [this] { mFrame->step(); }};
        }

        template<typename Value, typename Error>
        Awaitable<Value, Error>
        await_transform(
            Task<Value, Error> &task,
            const std::source_location location = std::source_location::current()
        ) {
            task.mFrame->parent = mFrame;
            mFrame->children.push_back(task.mFrame);
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = task.cancel();

            return {task.future(), [this] { mFrame->step(); }};
        }

        Awaitable<void>
        await_transform(TaskGroup &group, const std::source_location location = std::source_location::current()) {
            if (group.mFrames.empty())
                return {Future<void>::resolved()};

            const auto promise = std::make_shared<asyncio::Promise<void>>();
            const auto count = std::make_shared<std::size_t>(group.mFrames.size());

            for (const auto &frame: group.mFrames) {
                frame->parent = mFrame;

                auto callback = [=] {
                    if (--*count > 0)
                        return;

                    promise->resolve();
                };

                /*
                 * All tasks may have all been completed, but the callbacks have not yet been executed.
                 * We cannot allow the `co_await group` to complete synchronously,
                 * otherwise the upper-level coroutine might return immediately, causing the task group to be destroyed.
                 */
                if (frame->finished) {
                    mFrame->eventLoop->post(std::move(callback));
                    continue;
                }

                frame->callbacks.emplace_back(std::move(callback));
            }

            mFrame->children = group.mFrames;
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = group.cancel();

            return {promise->getFuture().via(mFrame->eventLoop), [this] { mFrame->step(); }};
        }

    protected:
        std::shared_ptr<Frame> mFrame;
        std::shared_ptr<asyncio::Promise<T, E>> mPromise;
    };

    template<typename T, typename E>
    class Promise final : public PromiseBase<T, E> {
    public:
        template<typename U = T>
        void return_value(U &&value) requires std::same_as<E, std::exception_ptr> {
            this->mFrame->end();
            this->mPromise->resolve(std::forward<U>(value));
        }

        void return_value(std::expected<T, E> &&result) requires (!std::same_as<E, std::exception_ptr>) {
            this->mFrame->end();

            if (!result) {
                this->mPromise->reject(std::move(result).error());
                return;
            }

            if constexpr (std::is_void_v<T>)
                this->mPromise->resolve();
            else
                this->mPromise->resolve(std::move(result).value());
        }

        void return_value(const std::expected<T, E> &result) requires (!std::same_as<E, std::exception_ptr>) {
            this->mFrame->end();

            if (!result) {
                this->mPromise->reject(result.error());
                return;
            }

            if constexpr (std::is_void_v<T>)
                this->mPromise->resolve();
            else
                this->mPromise->resolve(result.value());
        }
    };

    template<>
    class Promise<void, std::exception_ptr> final : public PromiseBase<void, std::exception_ptr> {
    public:
        void return_void() {
            this->mFrame->end();
            this->mPromise->resolve();
        }
    };

    template<std::input_iterator I, std::sentinel_for<I> S>
    using AllRangesFuture = decltype(
        zero::async::promise::all(
            std::ranges::subrange{std::declval<I>(), std::declval<S>()} | std::views::transform([](auto &task) {
                return task.future();
            })
        )
    );

    template<std::input_iterator I, std::sentinel_for<I> S>
    using AllRangesValue = AllRangesFuture<I, S>::value_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
    using AllRangesError = AllRangesFuture<I, S>::error_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
        requires zero::meta::Specialization<std::iter_value_t<I>, Task>
    Task<
        AllRangesValue<I, S>,
        AllRangesError<I, S>
    >
    all(I first, S last) {
        assert(first != last);

        using T = std::iter_value_t<I>::value_type;
        using E = std::iter_value_t<I>::error_type;

        TaskGroup group;
        std::list<Future<T, E>> futures;

        while (first != last) {
            group.add(*first);
            futures.push_back(first->future());
            ++first;
        }

        auto future = zero::async::promise::all(std::move(futures)).via(getEventLoop()).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;
        co_return co_await std::move(future);
    }

    template<std::ranges::input_range R>
        requires zero::meta::Specialization<std::ranges::range_value_t<R>, Task>
    auto all(R &&tasks) {
        return all(tasks.begin(), tasks.end());
    }

    template<typename... Ts>
    using AllVariadicFuture = decltype(zero::async::promise::all(std::declval<Ts>().future()...));

    template<typename... Ts>
    using AllVariadicValue = AllVariadicFuture<Ts...>::value_type;

    template<typename... Ts>
    using AllVariadicError = AllVariadicFuture<Ts...>::error_type;

    template<typename... Ts>
        requires (zero::meta::Specialization<std::remove_cvref_t<Ts>, Task> && ...)
    Task<
        AllVariadicValue<Ts...>,
        AllVariadicError<Ts...>
    >
    all(Ts &&... tasks) {
        static_assert(sizeof...(Ts) > 0);

        TaskGroup group;

        (group.add(tasks), ...);

        auto future = zero::async::promise::all(
            tasks.future()...
        ).via(getEventLoop()).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;
        co_return co_await std::move(future);
    }

    template<std::input_iterator I, std::sentinel_for<I> S>
    using AllSettledRangesFuture = decltype(
        zero::async::promise::allSettled(
            std::ranges::subrange{std::declval<I>(), std::declval<S>()} | std::views::transform([](auto &task) {
                return task.future();
            })
        )
    );

    template<std::input_iterator I, std::sentinel_for<I> S>
    using AllSettledRangesValue = AllSettledRangesFuture<I, S>::value_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
        requires zero::meta::Specialization<std::iter_value_t<I>, Task>
    Task<AllSettledRangesValue<I, S>>
    allSettled(I first, S last) {
        assert(first != last);

        using T = std::iter_value_t<I>::value_type;
        using E = std::iter_value_t<I>::error_type;

        TaskGroup group;
        std::list<Future<T, E>> futures;

        while (first != last) {
            group.add(*first);
            futures.push_back(first->future());
            ++first;
        }

        auto future = zero::async::promise::allSettled(std::move(futures)).via(getEventLoop()).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;
        co_return co_await std::move(future);
    }

    template<std::ranges::input_range R>
        requires zero::meta::Specialization<std::ranges::range_value_t<R>, Task>
    auto allSettled(R &&tasks) {
        return allSettled(tasks.begin(), tasks.end());
    }

    template<typename... Ts>
    using AllSettledVariadicFuture = decltype(zero::async::promise::allSettled(std::declval<Ts>().future()...));

    template<typename... Ts>
    using AllSettledVariadicValue = AllSettledVariadicFuture<Ts...>::value_type;

    template<typename... Ts>
        requires (zero::meta::Specialization<std::remove_cvref_t<Ts>, Task> && ...)
    Task<AllSettledVariadicValue<Ts...>>
    allSettled(Ts &&... tasks) {
        static_assert(sizeof...(Ts) > 0);

        TaskGroup group;

        (group.add(tasks), ...);

        auto future = zero::async::promise::allSettled(
            tasks.future()...
        ).via(getEventLoop()).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;
        co_return co_await std::move(future);
    }

    template<std::input_iterator I, std::sentinel_for<I> S>
    using AnyRangesFuture = decltype(
        zero::async::promise::any(
            std::ranges::subrange{std::declval<I>(), std::declval<S>()} | std::views::transform([](auto &task) {
                return task.future();
            })
        )
    );

    template<std::input_iterator I, std::sentinel_for<I> S>
    using AnyRangesValue = AnyRangesFuture<I, S>::value_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
    using AnyRangesError = AnyRangesFuture<I, S>::error_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
        requires zero::meta::Specialization<std::iter_value_t<I>, Task>
    Task<
        AnyRangesValue<I, S>,
        AnyRangesError<I, S>
    >
    any(I first, S last) {
        assert(first != last);

        using T = std::iter_value_t<I>::value_type;
        using E = std::iter_value_t<I>::error_type;

        TaskGroup group;
        std::list<Future<T, E>> futures;

        while (first != last) {
            group.add(*first);
            futures.push_back(first->future());
            ++first;
        }

        auto future = zero::async::promise::any(std::move(futures)).via(getEventLoop()).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;
        co_return co_await std::move(future);
    }

    template<std::ranges::input_range R>
        requires zero::meta::Specialization<std::ranges::range_value_t<R>, Task>
    auto any(R &&tasks) {
        return any(tasks.begin(), tasks.end());
    }

    template<typename... Ts>
    using AnyVariadicFuture = decltype(zero::async::promise::any(std::declval<Ts>().future()...));

    template<typename... Ts>
    using AnyVariadicValue = AnyVariadicFuture<Ts...>::value_type;

    template<typename... Ts>
    using AnyVariadicError = AnyVariadicFuture<Ts...>::error_type;

    template<typename... Ts>
        requires (zero::meta::Specialization<std::remove_cvref_t<Ts>, Task> && ...)
    Task<
        AnyVariadicValue<Ts...>,
        AnyVariadicError<Ts...>
    >
    any(Ts &&... tasks) {
        static_assert(sizeof...(Ts) > 0);

        TaskGroup group;

        (group.add(tasks), ...);

        auto future = zero::async::promise::any(
            tasks.future()...
        ).via(getEventLoop()).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;
        co_return co_await std::move(future);
    }

    template<std::input_iterator I, std::sentinel_for<I> S>
    using RaceRangesFuture = decltype(
        zero::async::promise::race(
            std::ranges::subrange{std::declval<I>(), std::declval<S>()} | std::views::transform([](auto &task) {
                return task.future();
            })
        )
    );

    template<std::input_iterator I, std::sentinel_for<I> S>
    using RaceRangesValue = RaceRangesFuture<I, S>::value_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
    using RaceRangesError = RaceRangesFuture<I, S>::error_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
        requires zero::meta::Specialization<std::iter_value_t<I>, Task>
    Task<
        RaceRangesValue<I, S>,
        RaceRangesError<I, S>
    >
    race(I first, S last) {
        assert(first != last);

        using T = std::iter_value_t<I>::value_type;
        using E = std::iter_value_t<I>::error_type;

        TaskGroup group;
        std::list<Future<T, E>> futures;

        while (first != last) {
            group.add(*first);
            futures.push_back(first->future());
            ++first;
        }

        auto future = zero::async::promise::race(std::move(futures)).via(getEventLoop()).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;
        co_return co_await std::move(future);
    }

    template<std::ranges::input_range R>
        requires zero::meta::Specialization<std::ranges::range_value_t<R>, Task>
    auto race(R &&tasks) {
        return race(tasks.begin(), tasks.end());
    }

    template<typename... Ts>
    using RaceVariadicFuture = decltype(zero::async::promise::race(std::declval<Ts>().future()...));

    template<typename... Ts>
    using RaceVariadicValue = RaceVariadicFuture<Ts...>::value_type;

    template<typename... Ts>
    using RaceVariadicError = RaceVariadicFuture<Ts...>::error_type;

    template<typename... Ts>
        requires (zero::meta::Specialization<std::remove_cvref_t<Ts>, Task> && ...)
    Task<
        RaceVariadicValue<Ts...>,
        RaceVariadicError<Ts...>
    >
    race(Ts &&... tasks) {
        static_assert(sizeof...(Ts) > 0);

        TaskGroup group;

        (group.add(tasks), ...);

        auto future = zero::async::promise::race(
            tasks.future()...
        ).via(getEventLoop()).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;
        co_return co_await std::move(future);
    }

    template<typename T, typename E>
    Task<T, E> from(SemiFuture<T, E> future) {
        co_return co_await std::move(future);
    }

    template<typename T, typename E>
    Task<T, E> from(Future<T, E> future) {
        co_return co_await std::move(future);
    }

    template<typename T, typename E>
    Task<T, E> from(Cancellable<SemiFuture<T, E>> cancellable) {
        co_return co_await std::move(cancellable);
    }

    template<typename T, typename E>
    Task<T, E> from(Cancellable<Future<T, E>> cancellable) {
        co_return co_await std::move(cancellable);
    }

    template<typename T, typename E>
    Task<T, E> from(Cancellable<Task<T, E>> cancellable) {
        co_return co_await std::move(cancellable);
    }

    template<Invocable F>
    std::invoke_result_t<F> spawn(F f) {
        co_return co_await std::invoke(std::move(f));
    }
}

Z_DECLARE_ERROR_CODE(asyncio::task::Error)

#endif //ASYNCIO_TASK_H

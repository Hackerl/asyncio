#ifndef ASYNCIO_TASK_H
#define ASYNCIO_TASK_H

#include "promise.h"
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
        CANCELLED, "Task was cancelled", std::errc::operation_canceled,
        CANCELLATION_NOT_SUPPORTED, "Task does not support cancellation", std::errc::operation_not_supported,
        LOCKED, "Task is locked", std::errc::resource_unavailable_try_again,
        CANCELLATION_TOO_LATE, "Cancellation is too late", std::errc::operation_not_permitted,
        ALREADY_COMPLETED, "Task is already completed", std::errc::operation_not_permitted
    )

    template<typename T, typename E>
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

        template<typename = void>
            requires (!std::is_same_v<E, std::exception_ptr>)
        std::expected<T, E> await_resume() {
            return std::move(*result);
        }

        template<typename = void>
            requires std::is_same_v<E, std::exception_ptr>
        T await_resume() {
            if (!result->has_value())
                std::rethrow_exception(result->error());

            if constexpr (!std::is_void_v<T>)
                return *std::move(*result);
            else
                return;
        }

        zero::async::promise::Future<T, E> future;
        std::function<void()> onReady;
        std::optional<std::expected<T, E>> result;
    };

    template<typename T, typename E>
    struct NoExceptAwaitable {
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

        std::expected<T, E> await_resume() {
            return std::move(*result);
        }

        zero::async::promise::Future<T, E> future;
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
        bool finished{false};
        bool locked{false};
        bool cancelled{false};

        void step();
        void end();
        std::expected<void, std::error_code> cancelAll();

        [[nodiscard]] tree<std::source_location> callTree() const;
        [[nodiscard]] std::string trace() const;
    };

    template<typename T, typename E>
    struct CancellableFuture {
        zero::async::promise::Future<T, E> future;
        std::function<std::expected<void, std::error_code>()> cancel;
    };

    template<typename T, typename E>
    struct CancellableTask {
        Task<T, E> task;
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
    using callback_result_t = zero::async::promise::callback_result_t<F, T>;

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

        template<typename Self>
            requires (!std::is_const_v<Self>)
        Self &&addCallback(this Self &&self, std::function<void()> callback) {
            if (self.done()) {
                zero::error::guard(getEventLoop()->post([callback = std::move(callback)] {
                    callback();
                }));

                return std::forward<Self>(self);
            }

            self.mFrame->callbacks.push_back(std::move(callback));
            return std::forward<Self>(self);
        }

        template<typename F>
            requires (
                !std::is_same_v<E, std::exception_ptr> &&
                zero::detail::is_specialization_v<callback_result_t<F, T>, Task>
            )
        Task<typename callback_result_t<F, T>::value_type, E> transform(F f) && {
            static_assert(
                std::is_same_v<typename callback_result_t<F, T>::error_type, std::exception_ptr>
            );

            auto result = co_await *this;

            if (!result)
                co_return std::unexpected{std::move(result).error()};

            if constexpr (std::is_void_v<T>)
                co_return co_await f();
            else
                co_return co_await std::invoke(f, *std::move(result));
        }

        template<typename F>
            requires (
                !std::is_same_v<E, std::exception_ptr> &&
                !zero::detail::is_specialization_v<callback_result_t<F, T>, Task>
            )
        Task<callback_result_t<F, T>, E> transform(F f) && {
            co_return (co_await *this).transform(f);
        }

        template<typename F>
            requires (
                !std::is_same_v<E, std::exception_ptr> &&
                zero::detail::is_specialization_v<callback_result_t<F, T>, Task>
            )
        Task<typename callback_result_t<F, T>::value_type, E> andThen(F f) && {
            static_assert(std::is_same_v<typename callback_result_t<F, T>::error_type, E>);

            auto result = co_await *this;

            if (!result)
                co_return std::unexpected{std::move(result).error()};

            if constexpr (std::is_void_v<T>)
                co_return co_await f();
            else
                co_return co_await std::invoke(f, *std::move(result));
        }

        template<typename F>
            requires (
                !std::is_same_v<E, std::exception_ptr> &&
                zero::detail::is_specialization_v<callback_result_t<F, T>, std::expected>
            )
        Task<typename callback_result_t<F, T>::value_type, E> andThen(F f) && {
            co_return (co_await *this).and_then(f);
        }

        template<typename F>
            requires (
                !std::is_same_v<E, std::exception_ptr> &&
                zero::detail::is_specialization_v<callback_result_t<F, E>, Task>
            )
        Task<T, typename callback_result_t<F, E>::value_type> transformError(F f) && {
            static_assert(
                std::is_same_v<typename callback_result_t<F, E>::error_type, std::exception_ptr>
            );
            auto result = co_await *this;

            if (!result)
                co_return std::unexpected{co_await std::invoke(f, std::move(result).error())};

            if constexpr (std::is_void_v<T>)
                co_return {};
            else
                co_return *std::move(result);
        }

        template<typename F>
            requires (
                !std::is_same_v<E, std::exception_ptr> &&
                !zero::detail::is_specialization_v<callback_result_t<F, E>, Task>
            )
        Task<T, callback_result_t<F, E>> transformError(F f) && {
            co_return (co_await *this).transform_error(f);
        }

        template<typename F>
            requires (
                !std::is_same_v<E, std::exception_ptr> &&
                zero::detail::is_specialization_v<callback_result_t<F, E>, Task>
            )
        Task<T, typename callback_result_t<F, E>::error_type> orElse(F f) && {
            static_assert(std::is_same_v<typename callback_result_t<F, E>::value_type, T>);
            auto result = co_await *this;

            if (!result)
                co_return co_await std::invoke(f, std::move(result).error());

            if constexpr (std::is_void_v<T>)
                co_return {};
            else
                co_return *std::move(result);
        }

        template<typename F>
            requires (
                !std::is_same_v<E, std::exception_ptr> &&
                zero::detail::is_specialization_v<callback_result_t<F, E>, std::expected>
            )
        Task<T, typename callback_result_t<F, E>::error_type> orElse(F f) && {
            co_return (co_await *this).or_else(f);
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

        zero::async::promise::Future<T, E> future() {
            return mPromise->getFuture();
        }

    private:
        std::shared_ptr<Frame> mFrame;
        std::shared_ptr<asyncio::Promise<T, E>> mPromise;

        template<typename, typename>
        friend class Promise;

        friend class TaskGroup;
    };

    class TaskGroup {
    public:
        [[nodiscard]] bool cancelled() const;

        std::expected<void, std::error_code> cancel();

        template<typename T>
            requires zero::detail::is_specialization_v<std::remove_cvref_t<T>, Task>
        void add(T &&task) {
            if (mCancelled)
                std::ignore = task.cancel();

            auto frame = task.mFrame;
            mFrames.push_back(frame);

            task.addCallback([frame = std::move(frame), this] {
                mFrames.remove(frame);
            });
        }

    private:
        bool mCancelled{false};
        std::list<std::shared_ptr<Frame>> mFrames;

        template<typename T, typename E>
        friend class Promise;

        friend struct Frame;
    };

    template<typename T, typename E>
    class Promise {
    public:
        Promise() : mFrame{std::make_shared<Frame>()}, mPromise{std::make_shared<asyncio::Promise<T, E>>()} {
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

        [[nodiscard]] Awaitable<bool, std::exception_ptr> await_transform(const Cancelled) const {
            return {zero::async::promise::resolve<bool, std::exception_ptr>(mFrame->cancelled)};
        }

        [[nodiscard]] Awaitable<void, std::exception_ptr> await_transform(const Lock) const {
            mFrame->locked = true;
            return {zero::async::promise::resolve<void, std::exception_ptr>()};
        }

        [[nodiscard]] Awaitable<void, std::exception_ptr> await_transform(const Unlock) const {
            assert(mFrame->locked);
            mFrame->locked = false;
            return {zero::async::promise::resolve<void, std::exception_ptr>()};
        }

        [[nodiscard]] Awaitable<std::vector<std::source_location>, std::exception_ptr>
        await_transform(const Backtrace, const std::source_location location = std::source_location::current()) const {
            std::vector stacktrace{location};

            auto frame = mFrame->parent.lock();

            while (frame) {
                assert(frame->location);
                stacktrace.push_back(*frame->location);
                frame = frame->parent.lock();
            }

            return {zero::async::promise::resolve<std::vector<std::source_location>, std::exception_ptr>(stacktrace)};
        }

        template<typename Result, typename Error>
        NoExceptAwaitable<Result, Error>
        await_transform(
            CancellableFuture<Result, Error> cancellable,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = cancellable.cancel();
            else
                mFrame->cancel = std::move(cancellable.cancel);

            return {std::move(cancellable.future), [this] { mFrame->step(); }};
        }

        template<typename Result, typename Error>
        Awaitable<Result, Error>
        await_transform(
            CancellableTask<Result, Error> cancellable,
            const std::source_location location = std::source_location::current()
        ) {
            cancellable.task.mFrame->parent = mFrame;
            mFrame->children.push_back(cancellable.task.mFrame);
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = cancellable.cancel();
            else
                mFrame->cancel = std::move(cancellable.cancel);

            return {cancellable.task.future(), [this] { mFrame->step(); }};
        }

        template<typename Result, typename Error>
        NoExceptAwaitable<Result, Error>
        await_transform(
            zero::async::promise::Future<Result, Error> future,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->location = location;
            return {std::move(future), [this] { mFrame->step(); }};
        }

        template<typename Result, typename Error>
        Awaitable<Result, Error>
        await_transform(
            Task<Result, Error> &&task,
            const std::source_location location = std::source_location::current()
        ) {
            task.mFrame->parent = mFrame;
            mFrame->children.push_back(task.mFrame);
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = task.cancel();

            return {task.future(), [this] { mFrame->step(); }};
        }

        template<typename Result, typename Error>
        Awaitable<Result, Error>
        await_transform(
            Task<Result, Error> &task,
            const std::source_location location = std::source_location::current()
        ) {
            task.mFrame->parent = mFrame;
            mFrame->children.push_back(task.mFrame);
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = task.cancel();

            return {task.future(), [this] { mFrame->step(); }};
        }

        Awaitable<void, std::exception_ptr>
        await_transform(TaskGroup &group, const std::source_location location = std::source_location::current()) {
            if (group.mFrames.empty())
                return {zero::async::promise::resolve<void, std::exception_ptr>()};

            const auto promise = std::make_shared<asyncio::Promise<void, std::exception_ptr>>();
            const auto count = std::make_shared<std::size_t>(group.mFrames.size());

            for (const auto &frame: group.mFrames) {
                frame->parent = mFrame;

                if (frame->finished) {
                    --*count;
                    continue;
                }

                frame->callbacks.emplace_back([=] {
                    if (--*count > 0)
                        return;

                    promise->resolve();
                });
            }

            mFrame->children = group.mFrames;
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = group.cancel();

            return {promise->getFuture(), [this] { mFrame->step(); }};
        }

        template<typename U = T>
            requires std::is_same_v<E, std::exception_ptr>
        void return_value(U &&value) {
            mFrame->end();
            mPromise->resolve(std::forward<U>(value));
        }

        template<typename = void>
            requires (!std::is_same_v<E, std::exception_ptr>)
        void return_value(std::expected<T, E> &&result) {
            mFrame->end();

            if (!result) {
                mPromise->reject(std::move(result).error());
                return;
            }

            if constexpr (std::is_void_v<T>)
                mPromise->resolve();
            else
                mPromise->resolve(std::move(result).value());
        }

        template<typename = void>
            requires (!std::is_same_v<E, std::exception_ptr>)
        void return_value(const std::expected<T, E> &result) {
            mFrame->end();

            if (!result) {
                mPromise->reject(result.error());
                return;
            }

            if constexpr (std::is_void_v<T>)
                mPromise->resolve();
            else
                mPromise->resolve(result.value());
        }

    private:
        std::shared_ptr<Frame> mFrame;
        std::shared_ptr<asyncio::Promise<T, E>> mPromise;
    };

    template<>
    class Promise<void, std::exception_ptr> {
    public:
        Promise(): mFrame{std::make_shared<Frame>()},
                   mPromise{std::make_shared<asyncio::Promise<void, std::exception_ptr>>()} {
        }

        // ReSharper disable once CppMemberFunctionMayBeStatic
        std::suspend_never initial_suspend() {
            return {};
        }

        // ReSharper disable once CppMemberFunctionMayBeStatic
        std::suspend_never final_suspend() noexcept {
            return {};
        }

        void unhandled_exception() {
            mFrame->end();
            mPromise->reject(std::current_exception());
        }

        [[nodiscard]] Task<void> get_return_object() {
            return {mFrame, mPromise};
        }

        [[nodiscard]] Awaitable<bool, std::exception_ptr> await_transform(const Cancelled) const {
            return {zero::async::promise::resolve<bool, std::exception_ptr>(mFrame->cancelled)};
        }

        [[nodiscard]] Awaitable<void, std::exception_ptr> await_transform(const Lock) const {
            mFrame->locked = true;
            return {zero::async::promise::resolve<void, std::exception_ptr>()};
        }

        [[nodiscard]] Awaitable<void, std::exception_ptr> await_transform(const Unlock) const {
            assert(mFrame->locked);
            mFrame->locked = false;
            return {zero::async::promise::resolve<void, std::exception_ptr>()};
        }

        [[nodiscard]] Awaitable<std::vector<std::source_location>, std::exception_ptr>
        await_transform(const Backtrace, const std::source_location location = std::source_location::current()) const {
            std::vector stacktrace{location};

            auto frame = mFrame->parent.lock();

            while (frame) {
                assert(frame->location);
                stacktrace.push_back(*frame->location);
                frame = frame->parent.lock();
            }

            return {zero::async::promise::resolve<std::vector<std::source_location>, std::exception_ptr>(stacktrace)};
        }

        template<typename Result, typename Error>
        NoExceptAwaitable<Result, Error>
        await_transform(
            CancellableFuture<Result, Error> cancellable,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = cancellable.cancel();
            else
                mFrame->cancel = std::move(cancellable.cancel);

            return {std::move(cancellable.future), [this] { mFrame->step(); }};
        }

        template<typename Result, typename Error>
        Awaitable<Result, Error>
        await_transform(
            CancellableTask<Result, Error> cancellable,
            const std::source_location location = std::source_location::current()
        ) {
            cancellable.task.mFrame->parent = mFrame;
            mFrame->children.push_back(cancellable.task.mFrame);
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = cancellable.cancel();
            else
                mFrame->cancel = std::move(cancellable.cancel);

            return {cancellable.task.future(), [this] { mFrame->step(); }};
        }

        template<typename Result, typename Error>
        NoExceptAwaitable<Result, Error>
        await_transform(
            zero::async::promise::Future<Result, Error> future,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->location = location;
            return {std::move(future), [this] { mFrame->step(); }};
        }

        template<typename Result, typename Error>
        Awaitable<Result, Error>
        await_transform(
            Task<Result, Error> &&task,
            const std::source_location location = std::source_location::current()
        ) {
            task.mFrame->parent = mFrame;
            mFrame->children.push_back(task.mFrame);
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = task.cancel();

            return {task.future(), [this] { mFrame->step(); }};
        }

        template<typename Result, typename Error>
        Awaitable<Result, Error>
        await_transform(
            Task<Result, Error> &task,
            const std::source_location location = std::source_location::current()
        ) {
            task.mFrame->parent = mFrame;
            mFrame->children.push_back(task.mFrame);
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = task.cancel();

            return {task.future(), [this] { mFrame->step(); }};
        }

        Awaitable<void, std::exception_ptr>
        await_transform(TaskGroup &group, const std::source_location location = std::source_location::current()) {
            if (group.mFrames.empty())
                return {zero::async::promise::resolve<void, std::exception_ptr>()};

            const auto promise = std::make_shared<asyncio::Promise<void, std::exception_ptr>>();
            const auto count = std::make_shared<std::size_t>(group.mFrames.size());

            for (const auto &frame: group.mFrames) {
                frame->parent = mFrame;

                if (frame->finished) {
                    --*count;
                    continue;
                }

                frame->callbacks.emplace_back([=] {
                    if (--*count > 0)
                        return;

                    promise->resolve();
                });
            }

            mFrame->children = group.mFrames;
            mFrame->location = location;

            if (mFrame->cancelled && !mFrame->locked)
                std::ignore = group.cancel();

            return {promise->getFuture(), [this] { mFrame->step(); }};
        }

        void return_void() {
            mFrame->end();
            mPromise->resolve();
        }

    private:
        std::shared_ptr<Frame> mFrame;
        std::shared_ptr<asyncio::Promise<void, std::exception_ptr>> mPromise;
    };

    template<std::input_iterator I, std::sentinel_for<I> S>
    using all_ranges_future_t = decltype(
        zero::async::promise::all(
            std::ranges::subrange{std::declval<I>(), std::declval<S>()} | std::views::transform([](auto &task) {
                return task.future();
            })
        )
    );

    template<std::input_iterator I, std::sentinel_for<I> S>
    using all_ranges_value_t = all_ranges_future_t<I, S>::value_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
    using all_ranges_error_t = all_ranges_future_t<I, S>::error_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
        requires zero::detail::is_specialization_v<std::iter_value_t<I>, Task>
    Task<
        all_ranges_value_t<I, S>,
        all_ranges_error_t<I, S>
    >
    all(I first, S last) {
        assert(first != last);

        using T = all_ranges_value_t<I, S>;
        using E = all_ranges_error_t<I, S>;

        TaskGroup group;

        std::for_each(first, last, [&](auto &task) { group.add(task); });

        auto future = zero::async::promise::all(
            std::ranges::subrange{first, last} | std::views::transform([](auto &task) {
                return task.future();
            })
        ).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;

        auto &result = future.result();

        if constexpr (std::is_same_v<E, std::exception_ptr>) {
            if (!result)
                std::rethrow_exception(result.error());

            if constexpr (!std::is_void_v<T>)
                co_return *std::move(result);
        }
        else {
            co_return std::move(result);
        }
    }

    template<std::ranges::range R>
        requires zero::detail::is_specialization_v<std::ranges::range_value_t<R>, Task>
    auto all(R &&tasks) {
        return all(tasks.begin(), tasks.end());
    }

    template<typename... Ts>
    using all_variadic_future_t = decltype(zero::async::promise::all(std::declval<Ts>().future()...));

    template<typename... Ts>
    using all_variadic_value_t = all_variadic_future_t<Ts...>::value_type;

    template<typename... Ts>
    using all_variadic_error_t = all_variadic_future_t<Ts...>::error_type;

    template<typename... Ts>
        requires (zero::detail::is_specialization_v<std::remove_cvref_t<Ts>, Task> && ...)
    Task<
        all_variadic_value_t<Ts...>,
        all_variadic_error_t<Ts...>
    >
    all(Ts &&... tasks) {
        static_assert(sizeof...(Ts) > 0);

        using T = all_variadic_value_t<Ts...>;
        using E = all_variadic_error_t<Ts...>;

        TaskGroup group;

        (group.add(tasks), ...);

        auto future = zero::async::promise::all(
            tasks.future()...
        ).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;

        auto &result = future.result();

        if constexpr (std::is_same_v<E, std::exception_ptr>) {
            if (!result)
                std::rethrow_exception(std::move(result).error());

            if constexpr (!std::is_void_v<T>)
                co_return *std::move(result);
        }
        else {
            co_return std::move(result);
        }
    }

    template<std::input_iterator I, std::sentinel_for<I> S>
    using all_settled_ranges_future_t = decltype(
        zero::async::promise::allSettled(
            std::ranges::subrange{std::declval<I>(), std::declval<S>()} | std::views::transform([](auto &task) {
                return task.future();
            })
        )
    );

    template<std::input_iterator I, std::sentinel_for<I> S>
    using all_settled_ranges_value_t = all_settled_ranges_future_t<I, S>::value_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
        requires zero::detail::is_specialization_v<std::iter_value_t<I>, Task>
    Task<all_settled_ranges_value_t<I, S>>
    allSettled(I first, S last) {
        assert(first != last);

        TaskGroup group;

        std::for_each(first, last, [&](auto &task) { group.add(task); });

        auto future = zero::async::promise::allSettled(
            std::ranges::subrange{first, last} | std::views::transform([](auto &task) {
                return task.future();
            })
        ).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;
        co_return *std::move(future).result();
    }

    template<std::ranges::range R>
        requires zero::detail::is_specialization_v<std::ranges::range_value_t<R>, Task>
    auto allSettled(R &&tasks) {
        return allSettled(tasks.begin(), tasks.end());
    }

    template<typename... Ts>
    using all_settled_variadic_future_t = decltype(zero::async::promise::allSettled(std::declval<Ts>().future()...));

    template<typename... Ts>
    using all_settled_variadic_value_t = all_settled_variadic_future_t<Ts...>::value_type;

    template<typename... Ts>
        requires (zero::detail::is_specialization_v<std::remove_cvref_t<Ts>, Task> && ...)
    Task<all_settled_variadic_value_t<Ts...>>
    allSettled(Ts &&... tasks) {
        static_assert(sizeof...(Ts) > 0);

        TaskGroup group;

        (group.add(tasks), ...);

        auto future = zero::async::promise::allSettled(
            tasks.future()...
        ).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;
        co_return *std::move(future).result();
    }

    template<std::input_iterator I, std::sentinel_for<I> S>
    using any_ranges_future_t = decltype(
        zero::async::promise::any(
            std::ranges::subrange{std::declval<I>(), std::declval<S>()} | std::views::transform([](auto &task) {
                return task.future();
            })
        )
    );

    template<std::input_iterator I, std::sentinel_for<I> S>
    using any_ranges_value_t = any_ranges_future_t<I, S>::value_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
    using any_ranges_error_t = any_ranges_future_t<I, S>::error_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
        requires zero::detail::is_specialization_v<std::iter_value_t<I>, Task>
    Task<
        any_ranges_value_t<I, S>,
        any_ranges_error_t<I, S>
    >
    any(I first, S last) {
        assert(first != last);

        TaskGroup group;

        std::for_each(first, last, [&](auto &task) { group.add(task); });

        auto future = zero::async::promise::any(
            std::ranges::subrange{first, last} | std::views::transform([](auto &task) {
                return task.future();
            })
        ).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;
        co_return std::move(future).result();
    }

    template<std::ranges::range R>
        requires zero::detail::is_specialization_v<std::ranges::range_value_t<R>, Task>
    auto any(R &&tasks) {
        return any(tasks.begin(), tasks.end());
    }

    template<typename... Ts>
    using any_variadic_future_t = decltype(zero::async::promise::any(std::declval<Ts>().future()...));

    template<typename... Ts>
    using any_variadic_value_t = any_variadic_future_t<Ts...>::value_type;

    template<typename... Ts>
    using any_variadic_error_t = any_variadic_future_t<Ts...>::error_type;

    template<typename... Ts>
        requires (zero::detail::is_specialization_v<std::remove_cvref_t<Ts>, Task> && ...)
    Task<
        any_variadic_value_t<Ts...>,
        any_variadic_error_t<Ts...>
    >
    any(Ts &&... tasks) {
        static_assert(sizeof...(Ts) > 0);

        TaskGroup group;

        (group.add(tasks), ...);

        auto future = zero::async::promise::any(
            tasks.future()...
        ).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;
        co_return std::move(future).result();
    }

    template<std::input_iterator I, std::sentinel_for<I> S>
    using race_ranges_future_t = decltype(
        zero::async::promise::race(
            std::ranges::subrange{std::declval<I>(), std::declval<S>()} | std::views::transform([](auto &task) {
                return task.future();
            })
        )
    );

    template<std::input_iterator I, std::sentinel_for<I> S>
    using race_ranges_value_t = race_ranges_future_t<I, S>::value_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
    using race_ranges_error_t = race_ranges_future_t<I, S>::error_type;

    template<std::input_iterator I, std::sentinel_for<I> S>
        requires zero::detail::is_specialization_v<std::iter_value_t<I>, Task>
    Task<
        race_ranges_value_t<I, S>,
        race_ranges_error_t<I, S>
    >
    race(I first, S last) {
        assert(first != last);

        using T = race_ranges_value_t<I, S>;
        using E = race_ranges_error_t<I, S>;

        TaskGroup group;

        std::for_each(first, last, [&](auto &task) { group.add(task); });

        auto future = zero::async::promise::race(
            std::ranges::subrange{first, last} | std::views::transform([](auto &task) {
                return task.future();
            })
        ).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;

        auto &result = future.result();

        if constexpr (std::is_same_v<E, std::exception_ptr>) {
            if (!result)
                std::rethrow_exception(result.error());

            if constexpr (!std::is_void_v<T>)
                co_return *std::move(result);
        }
        else {
            co_return std::move(result);
        }
    }

    template<std::ranges::range R>
        requires zero::detail::is_specialization_v<std::ranges::range_value_t<R>, Task>
    auto race(R &&tasks) {
        return race(tasks.begin(), tasks.end());
    }

    template<typename... Ts>
    using race_variadic_future_t = decltype(zero::async::promise::race(std::declval<Ts>().future()...));

    template<typename... Ts>
    using race_variadic_value_t = race_variadic_future_t<Ts...>::value_type;

    template<typename... Ts>
    using race_variadic_error_t = race_variadic_future_t<Ts...>::error_type;

    template<typename... Ts>
        requires (zero::detail::is_specialization_v<std::remove_cvref_t<Ts>, Task> && ...)
    Task<
        race_variadic_value_t<Ts...>,
        race_variadic_error_t<Ts...>
    >
    race(Ts &&... tasks) {
        static_assert(sizeof...(Ts) > 0);

        using T = race_variadic_value_t<Ts...>;
        using E = race_variadic_error_t<Ts...>;

        TaskGroup group;

        (group.add(tasks), ...);

        auto future = zero::async::promise::race(
            tasks.future()...
        ).finally([&] {
            std::ignore = group.cancel();
        });

        co_await group;

        auto &result = future.result();

        if constexpr (std::is_same_v<E, std::exception_ptr>) {
            if (!result)
                std::rethrow_exception(std::move(result).error());

            if constexpr (!std::is_void_v<T>)
                co_return *std::move(result);
        }
        else {
            co_return std::move(result);
        }
    }

    template<typename T, typename E>
    Task<T, E> from(zero::async::promise::Future<T, E> future) {
        auto result = co_await std::move(future);

        if constexpr (std::is_same_v<E, std::exception_ptr>) {
            if (!result)
                std::rethrow_exception(result.error());

            if constexpr (!std::is_void_v<T>)
                co_return *std::move(result);
        }
        else {
            co_return std::move(result);
        }
    }

    template<typename T, typename E>
    Task<T, E> from(CancellableFuture<T, E> cancellable) {
        auto result = co_await std::move(cancellable);

        if constexpr (std::is_same_v<E, std::exception_ptr>) {
            if (!result)
                std::rethrow_exception(result.error());

            if constexpr (!std::is_void_v<T>)
                co_return *std::move(result);
        }
        else {
            co_return std::move(result);
        }
    }

    template<typename T, typename E>
    Task<T, E> from(CancellableTask<T, E> cancellable) {
        co_return co_await std::move(cancellable);
    }

    template<typename F>
        requires zero::detail::is_specialization_v<std::invoke_result_t<F>, Task>
    std::invoke_result_t<F> spawn(F f) {
        co_return co_await f();
    }
}

Z_DECLARE_ERROR_CODE(asyncio::task::Error)

#endif //ASYNCIO_TASK_H

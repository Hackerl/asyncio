#ifndef ASYNCIO_TASK_H
#define ASYNCIO_TASK_H

#include "promise.h"
#include <algorithm>
#include <coroutine>
#include <exception>
#include <source_location>

namespace asyncio::task {
    DEFINE_ERROR_CODE_EX(
        Error,
        "asyncio::task",
        CANCELLED, "task has been cancelled", std::errc::operation_canceled,
        CANCELLATION_NOT_SUPPORTED, "task does not support cancellation", std::errc::operation_not_supported,
        LOCKED, "task has been locked", std::errc::resource_unavailable_try_again,
        WILL_BE_DONE, "operation will be done soon", DEFAULT_ERROR_CONDITION
    )

    template<typename T, typename E>
    struct Awaitable {
        [[nodiscard]] bool await_ready() {
            if (!future.isReady())
                return false;

            result.emplace(std::move(future).result());
            return true;
        }

        void await_suspend(const std::coroutine_handle<> handle) {
            future.setCallback([=, this](std::expected<T, E> res) {
                if (!res) {
                    result.emplace(std::unexpected(std::move(res).error()));
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
        std::optional<std::expected<T, E>> result;
    };

    template<typename T, typename E>
    struct NoExceptAwaitable {
        [[nodiscard]] bool await_ready() {
            if (!future.isReady())
                return false;

            result.emplace(std::move(future).result());
            return true;
        }

        void await_suspend(const std::coroutine_handle<> handle) {
            future.setCallback([=, this](std::expected<T, E> res) {
                if (!res) {
                    result.emplace(std::unexpected(std::move(res).error()));
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
        std::optional<std::expected<T, E>> result;
    };

    template<typename T, typename E>
    class Promise;

    struct Frame {
        std::shared_ptr<Frame> next;
        std::optional<std::source_location> location;
        std::function<std::expected<void, std::error_code>()> cancel;
        bool locked{};
        bool cancelled{};
    };

    template<typename T, typename E>
    struct Cancellable {
        zero::async::promise::Future<T, E> future;
        std::function<std::expected<void, std::error_code>()> cancel;
    };

    template<typename T, typename E>
    Cancellable(zero::async::promise::Future<T, E>, std::function<void()>) -> Cancellable<T, E>;

    struct Cancelled {
    };

    struct Lock {
    };

    struct Unlock {
    };

    inline constexpr Cancelled cancelled;
    inline constexpr Lock lock;
    inline constexpr Unlock unlock;

    template<typename T, typename E = std::exception_ptr>
    class Task {
    public:
        using value_type = T;
        using error_type = E;
        using promise_type = Promise<T, E>;

        Task(std::shared_ptr<Frame> frame, std::shared_ptr<asyncio::Promise<T, E>> promise)
            : mFrame(std::move(frame)), mPromise(std::move(promise)) {
        }

        Task(Task &&rhs) noexcept : mFrame(std::move(rhs.mFrame)), mPromise(std::move(rhs.mPromise)) {
        }

        Task &operator=(Task &&rhs) noexcept {
            mFrame = std::move(rhs.mFrame);
            mPromise = std::move(rhs.mPromise);
            return *this;
        }

        // ReSharper disable once CppMemberFunctionMayBeConst
        std::expected<void, std::error_code> cancel() {
            auto frame = mFrame;

            while (true) {
                frame->cancelled = true;

                if (frame->locked)
                    return std::unexpected(make_error_code(Error::LOCKED));

                if (!frame->next)
                    break;

                frame = frame->next;
            }

            if (!frame->cancel)
                return std::unexpected(make_error_code(Error::CANCELLATION_NOT_SUPPORTED));

            return std::exchange(frame->cancel, nullptr)();
        }

        [[nodiscard]] std::vector<std::source_location> traceback() const {
            std::vector<std::source_location> callstack;

            for (auto frame = mFrame; frame; frame = frame->next) {
                if (!frame->location)
                    break;

                callstack.push_back(*frame->location);
            }

            return callstack;
        }

        template<typename F, typename R = std::invoke_result_t<F>>
            requires (!std::is_same_v<E, std::exception_ptr> && std::is_void_v<T>)
        Task<typename R::value_type, E> andThen(F f) && {
            auto result = co_await std::move(*this);

            if constexpr (zero::detail::is_specialization<R, Task>) {
                if (!result)
                    co_return std::unexpected(std::move(result).error());

                co_return co_await std::invoke(std::move(f));
            }
            else {
                co_return std::move(result).and_then(std::move(f));
            }
        }

        template<typename F, typename R = std::invoke_result_t<F, T>>
            requires (!std::is_same_v<E, std::exception_ptr> && !std::is_void_v<T>)
        Task<typename R::value_type, E> andThen(F f) && {
            auto result = co_await std::move(*this);

            if constexpr (zero::detail::is_specialization<R, Task>) {
                if (!result)
                    co_return std::unexpected(std::move(result).error());

                co_return co_await std::invoke(std::move(f), *std::move(result));
            }
            else {
                co_return std::move(result).and_then(std::move(f));
            }
        }

        template<typename F, typename R = std::invoke_result_t<F>>
            requires (
                !std::is_same_v<E, std::exception_ptr> &&
                std::is_void_v<T> &&
                !zero::detail::is_specialization<R, Task>
            )
        Task<R, E> transform(F f) && {
            auto result = co_await std::move(*this);
            co_return std::move(result).transform(std::move(f));
        }

        template<typename F, typename R = std::invoke_result_t<F>>
            requires (
                !std::is_same_v<E, std::exception_ptr> &&
                std::is_void_v<T> &&
                zero::detail::is_specialization<R, Task> &&
                std::is_same_v<typename R::error_type, std::exception_ptr>
            )
        Task<typename R::value_type, E> transform(F f) && {
            auto result = co_await std::move(*this);

            if (!result)
                co_return std::unexpected(std::move(result).error());

            co_return co_await std::invoke(std::move(f));
        }

        template<typename F>
            requires (
                !std::is_same_v<E, std::exception_ptr> &&
                !std::is_void_v<T> &&
                !zero::detail::is_specialization<std::invoke_result_t<F, T>, Task>
            )
        Task<std::invoke_result_t<F, T>, E> transform(F f) && {
            auto result = co_await std::move(*this);
            co_return std::move(result).transform(std::move(f));
        }

        template<typename F, typename R = std::invoke_result_t<F, T>>
            requires (
                !std::is_same_v<E, std::exception_ptr> &&
                !std::is_void_v<T> &&
                zero::detail::is_specialization<R, Task> &&
                std::is_same_v<typename R::error_type, std::exception_ptr>
            )
        Task<typename R::value_type, E> transform(F f) && {
            auto result = co_await std::move(*this);

            if (!result)
                co_return std::unexpected(std::move(result).error());

            if constexpr (std::is_void_v<typename R::value_type>) {
                co_await std::invoke(std::move(f), *std::move(result));
                co_return std::expected<typename R::value_type, E>{};
            }
            else {
                co_return co_await std::invoke(std::move(f), *std::move(result));
            }
        }

        template<typename F, typename R = std::invoke_result_t<F, E>>
            requires (!std::is_same_v<E, std::exception_ptr>)
        Task<T, typename R::error_type> orElse(F f) && {
            auto result = co_await std::move(*this);

            if constexpr (zero::detail::is_specialization<R, Task>) {
                if (result)
                    co_return *std::move(result);

                co_return co_await std::invoke(std::move(f), std::move(result).error());
            }
            else {
                co_return std::move(result).or_else(std::move(f));
            }
        }

        template<typename F, typename R = std::invoke_result_t<F, E>>
            requires (!std::is_same_v<E, std::exception_ptr> && !zero::detail::is_specialization<R, Task>)
        Task<T, R> transformError(F f) && {
            auto result = co_await std::move(*this);
            co_return std::move(result).transform_error(std::move(f));
        }

        template<typename F, typename R = std::invoke_result_t<F, E>>
            requires (
                !std::is_same_v<E, std::exception_ptr> &&
                zero::detail::is_specialization<R, Task> &&
                std::is_same_v<typename R::error_type, std::exception_ptr>
            )
        Task<T, typename R::value_type> transformError(F f) && {
            auto result = co_await std::move(*this);

            if (result)
                co_return *std::move(result);

            co_return std::unexpected(co_await std::invoke(std::move(f), std::move(result).error()));
        }

        [[nodiscard]] bool done() const {
            return mPromise->isFulfilled();
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
    };

    template<typename T, typename E>
    class Promise {
    public:
        Promise() : mFrame(std::make_shared<Frame>()), mPromise(std::make_shared<asyncio::Promise<T, E>>()) {
        }

        std::suspend_never initial_suspend() {
            return {};
        }

        std::suspend_never final_suspend() noexcept {
            return {};
        }

        void unhandled_exception() {
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

        template<typename Result, typename Error>
        NoExceptAwaitable<Result, Error>
        await_transform(
            Cancellable<Result, Error> cancellable,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->next.reset();
            mFrame->location = location;
            mFrame->cancel = nullptr;

            if (mFrame->cancelled && !mFrame->locked)
                cancellable.cancel();
            else
                mFrame->cancel = std::move(cancellable.cancel);

            return {std::move(cancellable.future)};
        }

        template<typename Result, typename Error>
        NoExceptAwaitable<Result, Error>
        await_transform(
            zero::async::promise::Future<Result, Error> future,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->next.reset();
            mFrame->location = location;
            mFrame->cancel = nullptr;

            return {std::move(future)};
        }

        template<typename Result, typename Error>
        Awaitable<Result, Error>
        await_transform(
            Task<Result, Error> &&task,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->next = task.mFrame;
            mFrame->location = location;
            mFrame->cancel = nullptr;

            if (mFrame->cancelled && !mFrame->locked)
                task.cancel();

            return {task.future()};
        }

        template<typename Result, typename Error>
        Awaitable<Result, Error>
        await_transform(
            Task<Result, Error> &task,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->next = task.mFrame;
            mFrame->location = location;
            mFrame->cancel = nullptr;

            if (mFrame->cancelled && !mFrame->locked)
                task.cancel();

            return {task.future()};
        }

        template<typename U = T>
            requires std::is_same_v<E, std::exception_ptr>
        void return_value(U &&value) {
            mFrame->next.reset();
            mFrame->location.reset();
            mFrame->cancel = nullptr;

            mPromise->resolve(std::forward<U>(value));
        }

        template<typename = void>
            requires (!std::is_same_v<E, std::exception_ptr>)
        void return_value(std::expected<T, E> &&result) {
            mFrame->next.reset();
            mFrame->location.reset();
            mFrame->cancel = nullptr;

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
            mFrame->next.reset();
            mFrame->location.reset();
            mFrame->cancel = nullptr;

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
        Promise(): mFrame(std::make_shared<Frame>()),
                   mPromise(std::make_shared<asyncio::Promise<void, std::exception_ptr>>()) {
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

        template<typename Result, typename Error>
        NoExceptAwaitable<Result, Error>
        await_transform(
            Cancellable<Result, Error> cancellable,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->next.reset();
            mFrame->location = location;
            mFrame->cancel = nullptr;

            if (mFrame->cancelled && !mFrame->locked)
                cancellable.cancel();
            else
                mFrame->cancel = std::move(cancellable.cancel);

            return {std::move(cancellable.future)};
        }

        template<typename Result, typename Error>
        NoExceptAwaitable<Result, Error>
        await_transform(
            zero::async::promise::Future<Result, Error> future,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->next.reset();
            mFrame->location = location;
            mFrame->cancel = nullptr;

            return {std::move(future)};
        }

        template<typename Result, typename Error>
        Awaitable<Result, Error>
        await_transform(
            Task<Result, Error> &&task,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->next = task.mFrame;
            mFrame->location = location;
            mFrame->cancel = nullptr;

            if (mFrame->cancelled && !mFrame->locked)
                task.cancel();

            return {task.future()};
        }

        template<typename Result, typename Error>
        Awaitable<Result, Error>
        await_transform(
            Task<Result, Error> &task,
            const std::source_location location = std::source_location::current()
        ) {
            mFrame->next = task.mFrame;
            mFrame->location = location;
            mFrame->cancel = nullptr;

            if (mFrame->cancelled && !mFrame->locked)
                task.cancel();

            return {task.future()};
        }

        void return_void() {
            mFrame->next.reset();
            mFrame->location.reset();
            mFrame->cancel = nullptr;

            mPromise->resolve();
        }

    private:
        std::shared_ptr<Frame> mFrame;
        std::shared_ptr<asyncio::Promise<void, std::exception_ptr>> mPromise;
    };

    struct WaitContext {
        explicit WaitContext(const std::size_t n) : count(n) {
        }

        asyncio::Promise<void> promise;
        std::atomic<std::size_t> count;
    };

    template<std::input_iterator I, std::sentinel_for<I> S>
        requires zero::detail::is_specialization<std::iter_value_t<I>, Task>
    auto all(I first, S last) {
        using T = typename std::iter_value_t<I>::value_type;
        using E = typename std::iter_value_t<I>::error_type;

        std::list<std::shared_ptr<Task<T, E>>> subTasks;
        std::transform(
            std::move(first),
            std::move(last),
            std::back_inserter(subTasks),
            [](Task<T, E> &&task) {
                return std::make_shared<Task<T, E>>(std::move(task));
            }
        );

        return [](auto tasks) -> Task<std::conditional_t<std::is_void_v<T>, void, std::vector<T>>, E> {
            const auto ctx = std::make_shared<WaitContext>(tasks.size());

            auto result = co_await Cancellable{
                zero::async::promise::all(
                    tasks | std::views::transform([&](auto &task) {
                        return task->future().finally([=] {
                            if (--ctx->count > 0)
                                return;

                            ctx->promise.resolve();
                        });
                    })
                ),
                [=]() -> std::expected<void, std::error_code> {
                    std::error_code ec;

                    for (const auto &task: tasks) {
                        if (task->done() || task->cancelled())
                            continue;

                        if (const auto res = task->cancel(); !res)
                            ec = res.error();
                    }

                    if (ec)
                        return std::unexpected(ec);

                    return {};
                }
            };

            if (!result) {
                for (const auto &task: tasks) {
                    if (task->done() || task->cancelled())
                        continue;

                    task->cancel();
                }
            }

            co_await ctx->promise.getFuture();

            if constexpr (std::is_same_v<E, std::exception_ptr>) {
                if (!result)
                    std::rethrow_exception(result.error());

                if constexpr (!std::is_void_v<T>)
                    co_return *std::move(result);
            }
            else {
                co_return std::move(result);
            }
        }(std::move(subTasks));
    }

    template<std::ranges::range R>
        requires zero::detail::is_specialization<std::ranges::range_value_t<R>, Task>
    auto all(R subTasks) {
        return all(std::make_move_iterator(subTasks.begin()), std::make_move_iterator(subTasks.end()));
    }

    template<typename... Ts, typename E>
    auto all(Task<Ts, E>... subTasks) {
        using T = zero::async::promise::futures_result_t<Ts...>;

        return [](std::shared_ptr<Task<Ts, E>>... tasks) -> Task<T, E> {
            const auto ctx = std::make_shared<WaitContext>(sizeof...(Ts));

            auto result = co_await Cancellable{
                zero::async::promise::all(
                    tasks->future().finally([=] {
                        if (--ctx->count > 0)
                            return;

                        ctx->promise.resolve();
                    })...
                ),
                [=]() -> std::expected<void, std::error_code> {
                    std::error_code ec;

                    ([&] {
                        if (tasks->done() || tasks->cancelled())
                            return;

                        if (const auto res = tasks->cancel(); !res)
                            ec = res.error();
                    }(), ...);

                    if (ec)
                        return std::unexpected(ec);

                    return {};
                }
            };

            if (!result) {
                ([&] {
                    if (tasks->done() || tasks->cancelled())
                        return;

                    tasks->cancel();
                }(), ...);
            }

            co_await ctx->promise.getFuture();

            if constexpr (std::is_same_v<E, std::exception_ptr>) {
                if (!result)
                    std::rethrow_exception(result.error());

                if constexpr (!std::is_void_v<T>)
                    co_return *std::move(result);
            }
            else {
                co_return std::move(result);
            }
        }(std::make_shared<Task<Ts, E>>(std::move(subTasks))...);
    }

    template<std::input_iterator I, std::sentinel_for<I> S>
        requires zero::detail::is_specialization<std::iter_value_t<I>, Task>
    auto allSettled(I first, S last) {
        using T = typename std::iter_value_t<I>::value_type;
        using E = typename std::iter_value_t<I>::error_type;

        std::list<std::shared_ptr<Task<T, E>>> subTasks;
        std::transform(
            std::move(first),
            std::move(last),
            std::back_inserter(subTasks),
            [](Task<T, E> &&task) {
                return std::make_shared<Task<T, E>>(std::move(task));
            }
        );

        return [](auto tasks) -> Task<std::vector<std::expected<T, E>>> {
            co_return *co_await Cancellable{
                zero::async::promise::allSettled(
                    tasks | std::views::transform([](auto &task) {
                        return task->future();
                    })
                ),
                [=]() -> std::expected<void, std::error_code> {
                    std::error_code ec;

                    for (const auto &task: tasks) {
                        if (task->done() || task->cancelled())
                            continue;

                        if (const auto result = task->cancel(); !result)
                            ec = result.error();
                    }

                    if (ec)
                        return std::unexpected(ec);

                    return {};
                }
            };
        }(std::move(subTasks));
    }

    template<std::ranges::range R>
        requires zero::detail::is_specialization<std::ranges::range_value_t<R>, Task>
    auto allSettled(R subTasks) {
        return allSettled(std::make_move_iterator(subTasks.begin()), std::make_move_iterator(subTasks.end()));
    }

    template<typename... Ts, typename... Es>
    auto allSettled(Task<Ts, Es>... subTasks) {
        using T = std::conditional_t<
            zero::detail::all_same_v<Ts...>,
            std::array<
                std::expected<zero::detail::first_element_t<Ts...>, zero::detail::first_element_t<Es...>>,
                sizeof...(Ts)
            >,
            std::tuple<std::expected<Ts, Es>...>
        >;

        return [](std::shared_ptr<Task<Ts, Es>>... tasks) -> Task<T> {
            co_return *co_await Cancellable{
                zero::async::promise::allSettled(tasks->future()...),
                [=]() -> std::expected<void, std::error_code> {
                    std::error_code ec;

                    ([&] {
                        if (tasks->done() || tasks->cancelled())
                            return;

                        if (const auto result = tasks->cancel(); !result)
                            ec = result.error();
                    }(), ...);

                    if (ec)
                        return std::unexpected(ec);

                    return {};
                }
            };
        }(std::make_shared<Task<Ts, Es>>(std::move(subTasks))...);
    }

    template<std::input_iterator I, std::sentinel_for<I> S>
        requires zero::detail::is_specialization<std::iter_value_t<I>, Task>
    auto any(I first, S last) {
        using T = typename std::iter_value_t<I>::value_type;
        using E = typename std::iter_value_t<I>::error_type;

        std::list<std::shared_ptr<Task<T, E>>> subTasks;
        std::transform(
            std::move(first),
            std::move(last),
            std::back_inserter(subTasks),
            [](Task<T, E> &&task) {
                return std::make_shared<Task<T, E>>(std::move(task));
            }
        );

        return [](auto tasks) -> Task<T, std::vector<E>> {
            const auto ctx = std::make_shared<WaitContext>(tasks.size());

            auto result = co_await Cancellable{
                zero::async::promise::any(
                    tasks | std::views::transform([&](auto &task) {
                        return task->future().finally([=] {
                            if (--ctx->count > 0)
                                return;

                            ctx->promise.resolve();
                        });
                    })
                ),
                [=]() -> std::expected<void, std::error_code> {
                    std::error_code ec;

                    for (const auto &task: tasks) {
                        if (task->done() || task->cancelled())
                            continue;

                        if (const auto res = task->cancel(); !res)
                            ec = res.error();
                    }

                    if (ec)
                        return std::unexpected(ec);

                    return {};
                }
            };

            if (result) {
                for (const auto &task: tasks) {
                    if (task->done() || task->cancelled())
                        continue;

                    task->cancel();
                }
            }

            co_await ctx->promise.getFuture();
            co_return std::move(result);
        }(std::move(subTasks));
    }

    template<std::ranges::range R>
        requires zero::detail::is_specialization<std::ranges::range_value_t<R>, Task>
    auto any(R subTasks) {
        return any(std::make_move_iterator(subTasks.begin()), std::make_move_iterator(subTasks.end()));
    }

    template<typename... Ts, typename E>
    auto any(Task<Ts, E>... subTasks) {
        using T = std::conditional_t<zero::detail::all_same_v<Ts...>, zero::detail::first_element_t<Ts...>, std::any>;

        return [](std::shared_ptr<Task<Ts, E>>... tasks) -> Task<T, std::array<E, sizeof...(Ts)>> {
            const auto ctx = std::make_shared<WaitContext>(sizeof...(Ts));

            auto result = co_await Cancellable{
                zero::async::promise::any(
                    tasks->future().finally([=] {
                        if (--ctx->count > 0)
                            return;

                        ctx->promise.resolve();
                    })...
                ),
                [=]() -> std::expected<void, std::error_code> {
                    std::error_code ec;

                    ([&] {
                        if (tasks->done() || tasks->cancelled())
                            return;

                        if (const auto res = tasks->cancel(); !res)
                            ec = res.error();
                    }(), ...);

                    if (ec)
                        return std::unexpected(ec);

                    return {};
                }
            };

            if (result) {
                ([&] {
                    if (tasks->done() || tasks->cancelled())
                        return;

                    tasks->cancel();
                }(), ...);
            }

            co_await ctx->promise.getFuture();
            co_return std::move(result);
        }(std::make_shared<Task<Ts, E>>(std::move(subTasks))...);
    }

    template<std::input_iterator I, std::sentinel_for<I> S>
        requires zero::detail::is_specialization<std::iter_value_t<I>, Task>
    auto race(I first, S last) {
        using T = typename std::iter_value_t<I>::value_type;
        using E = typename std::iter_value_t<I>::error_type;

        std::list<std::shared_ptr<Task<T, E>>> subTasks;
        std::transform(
            std::move(first),
            std::move(last),
            std::back_inserter(subTasks),
            [](Task<T, E> &&task) {
                return std::make_shared<Task<T, E>>(std::move(task));
            }
        );

        return [](auto tasks) -> Task<T, E> {
            const auto ctx = std::make_shared<WaitContext>(tasks.size());

            auto result = co_await Cancellable{
                zero::async::promise::race(
                    tasks | std::views::transform([&](auto &task) {
                        return task->future().finally([=] {
                            if (--ctx->count > 0)
                                return;

                            ctx->promise.resolve();
                        });
                    })
                ),
                [=]() -> std::expected<void, std::error_code> {
                    std::error_code ec;

                    for (const auto &task: tasks) {
                        if (task->done() || task->cancelled())
                            continue;

                        if (const auto res = task->cancel(); !res)
                            ec = res.error();
                    }

                    if (ec)
                        return std::unexpected(ec);

                    return {};
                }
            };

            for (const auto &task: tasks) {
                if (task->done() || task->cancelled())
                    continue;

                task->cancel();
            }

            co_await ctx->promise.getFuture();

            if constexpr (std::is_same_v<E, std::exception_ptr>) {
                if (!result)
                    std::rethrow_exception(result.error());

                if constexpr (!std::is_void_v<T>)
                    co_return *std::move(result);
            }
            else {
                co_return std::move(result);
            }
        }(std::move(subTasks));
    }

    template<std::ranges::range R>
        requires zero::detail::is_specialization<std::ranges::range_value_t<R>, Task>
    auto race(R subTasks) {
        return race(std::make_move_iterator(subTasks.begin()), std::make_move_iterator(subTasks.end()));
    }

    template<typename... Ts, typename E>
    auto race(Task<Ts, E>... subTasks) {
        using T = std::conditional_t<zero::detail::all_same_v<Ts...>, zero::detail::first_element_t<Ts...>, std::any>;

        return [](std::shared_ptr<Task<Ts, E>>... tasks) -> Task<T, E> {
            const auto ctx = std::make_shared<WaitContext>(sizeof...(Ts));

            auto result = co_await Cancellable{
                zero::async::promise::race(
                    tasks->future().finally([=] {
                        if (--ctx->count > 0)
                            return;

                        ctx->promise.resolve();
                    })...
                ),
                [=]() -> std::expected<void, std::error_code> {
                    std::error_code ec;

                    ([&] {
                        if (tasks->done() || tasks->cancelled())
                            return;

                        if (const auto res = tasks->cancel(); !res)
                            ec = res.error();
                    }(), ...);

                    if (ec)
                        return std::unexpected(ec);

                    return {};
                }
            };

            ([&] {
                if (tasks->done() || tasks->cancelled())
                    return;

                tasks->cancel();
            }(), ...);

            co_await ctx->promise.getFuture();

            if constexpr (std::is_same_v<E, std::exception_ptr>) {
                if (!result)
                    std::rethrow_exception(result.error());

                if constexpr (!std::is_void_v<T>)
                    co_return *std::move(result);
            }
            else {
                co_return std::move(result);
            }
        }(std::make_shared<Task<Ts, E>>(std::move(subTasks))...);
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
    Task<T, E> from(Cancellable<T, E> cancellable) {
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
}

DECLARE_ERROR_CODE(asyncio::task::Error)

#endif //ASYNCIO_TASK_H

#ifndef ASYNCIO_EVENT_LOOP_H
#define ASYNCIO_EVENT_LOOP_H

#include <queue>
#include <event.h>
#include <event2/dns.h>
#include <zero/expect.h>
#include "worker.h"
#include "fs/framework.h"
#include "net/net.h"

namespace asyncio {
    constexpr auto DEFAULT_MAX_WORKER_NUMBER = 16;

    class EventLoop {
    public:
        enum class Error {
            INVALID_NAMESERVER = 1
        };

        class ErrorCategory final : public std::error_category {
        public:
            [[nodiscard]] const char *name() const noexcept override;
            [[nodiscard]] std::string message(int value) const override;
            [[nodiscard]] std::error_condition default_error_condition(int value) const noexcept override;
        };

        EventLoop(
            std::unique_ptr<event_base, decltype(event_base_free) *> base,
            std::unique_ptr<fs::IFramework> framework,
            std::size_t maxWorkers
        );

        static tl::expected<EventLoop, std::error_code> make(std::size_t maxWorkers = DEFAULT_MAX_WORKER_NUMBER);

        event_base *base();
        [[nodiscard]] const event_base *base() const;

        fs::IFramework &framework();
        [[nodiscard]] const fs::IFramework &framework() const;

        tl::expected<void, std::error_code> addNameserver(const char *ip);

        [[nodiscard]] tl::expected<std::unique_ptr<evdns_base, void (*)(evdns_base *)>, std::error_code>
        makeDNSBase() const;

        void dispatch() const;
        void loopBreak() const;
        void loopExit(std::optional<std::chrono::milliseconds> ms = std::nullopt) const;

        template<typename F>
        void post(F &&f, const std::optional<std::chrono::milliseconds> ms = std::nullopt) {
            std::optional<timeval> tv;

            if (ms)
                tv = {
                    static_cast<decltype(timeval::tv_sec)>(ms->count() / 1000),
                    static_cast<decltype(timeval::tv_usec)>(ms->count() % 1000 * 1000)
                };

            const auto ctx = new std::decay_t<F>(std::forward<F>(f));

            event_base_once(
                mBase.get(),
                -1,
                EV_TIMEOUT,
                [](evutil_socket_t, short, void *arg) {
                    auto func = static_cast<std::decay_t<F> *>(arg);

                    func->operator()();
                    delete func;
                },
                ctx,
                tv ? &*tv : nullptr
            );
        }

        template<typename F>
            requires zero::detail::is_specialization<std::invoke_result_t<F>, tl::expected>
        zero::async::coroutine::Task<
            typename std::invoke_result_t<F>::value_type,
            typename std::invoke_result_t<F>::error_type
        >
        toThread(F f) {
            using T = std::invoke_result_t<F>;

            std::unique_ptr<Worker> worker;

            if (mWorkers.empty()) {
                worker = std::make_unique<Worker>();
            }
            else {
                worker = std::move(mWorkers.front());
                mWorkers.pop();
            }

            std::optional<T> result;
            zero::async::promise::Promise<void> promise;

            worker->execute([&, f = std::move(f)] {
                result.emplace(f());
                post([&] {
                    promise.resolve();
                });
            });

            co_await promise.getFuture();

            if (mWorkers.size() < mMaxWorkers)
                mWorkers.push(std::move(worker));

            co_return *std::move(result);
        }

        template<typename F>
            requires (!zero::detail::is_specialization<std::invoke_result_t<F>, tl::expected>)
        zero::async::coroutine::Task<std::invoke_result_t<F>> toThread(F f) {
            using T = std::invoke_result_t<F>;

            std::unique_ptr<Worker> worker;

            if (mWorkers.empty()) {
                worker = std::make_unique<Worker>();
            }
            else {
                worker = std::move(mWorkers.front());
                mWorkers.pop();
            }

            std::optional<T> result;
            zero::async::promise::Promise<void> promise;

            worker->execute([&, f = std::move(f)] {
                result.emplace(f());
                post([&] {
                    promise.resolve();
                });
            });

            co_await promise.getFuture();

            if (mWorkers.size() < mMaxWorkers)
                mWorkers.push(std::move(worker));

            co_return *std::move(result);
        }

        template<typename F, typename C>
            requires zero::detail::is_specialization<std::invoke_result_t<F>, tl::expected>
        zero::async::coroutine::Task<
            typename std::invoke_result_t<F>::value_type,
            typename std::invoke_result_t<F>::error_type
        >
        toThread(F f, C cancel) {
            using T = std::invoke_result_t<F>;

            std::unique_ptr<Worker> worker;

            if (mWorkers.empty()) {
                worker = std::make_unique<Worker>();
            }
            else {
                worker = std::move(mWorkers.front());
                mWorkers.pop();
            }

            std::optional<T> result;
            zero::async::promise::Promise<void> promise;

            worker->execute([&, f = std::move(f)] {
                result.emplace(f());
                post([&] {
                    promise.resolve();
                });
            });

            co_await zero::async::coroutine::Cancellable{
                promise.getFuture(),
                [handle = worker->handle(), cancel = std::move(cancel)] {
                    return cancel(handle);
                }
            };

            if (mWorkers.size() < mMaxWorkers)
                mWorkers.push(std::move(worker));

            co_return *std::move(result);
        }

        template<typename F, typename C>
            requires (!zero::detail::is_specialization<std::invoke_result_t<F>, tl::expected>)
        zero::async::coroutine::Task<std::invoke_result_t<F>>
        toThread(F f, C cancel) {
            using T = std::invoke_result_t<F>;

            std::unique_ptr<Worker> worker;

            if (mWorkers.empty()) {
                worker = std::make_unique<Worker>();
            }
            else {
                worker = std::move(mWorkers.front());
                mWorkers.pop();
            }

            std::optional<T> result;
            zero::async::promise::Promise<void> promise;

            worker->execute([&, f = std::move(f)] {
                result.emplace(f());
                post([&] {
                    promise.resolve();
                });
            });

            co_await zero::async::coroutine::Cancellable{
                promise.getFuture(),
                [handle = worker->handle(), cancel = std::move(cancel)] {
                    return cancel(handle);
                }
            };

            if (mWorkers.size() < mMaxWorkers)
                mWorkers.push(std::move(worker));

            co_return *std::move(result);
        }

    private:
        std::size_t mMaxWorkers;
        std::queue<std::unique_ptr<Worker>> mWorkers;
        std::unique_ptr<event_base, decltype(event_base_free) *> mBase;
        std::unique_ptr<fs::IFramework> mFramework;
        std::list<net::Address> mNameservers;
    };

    std::error_code make_error_code(EventLoop::Error e);

    std::shared_ptr<EventLoop> getEventLoop();
    void setEventLoop(const std::weak_ptr<EventLoop> &eventLoop);

    template<typename F>
    auto toThread(F &&f) {
        return getEventLoop()->toThread(std::forward<F>(f));
    }

    template<typename F, typename C>
    auto toThread(F &&f, C &&cancel) {
        return getEventLoop()->toThread(std::forward<F>(f), std::forward<C>(cancel));
    }

    zero::async::coroutine::Task<void, std::error_code> sleep(std::chrono::milliseconds ms);

    enum class TimeoutError {
        ELAPSED = 1
    };

    class TimeoutErrorCategory final : public std::error_category {
    public:
        [[nodiscard]] const char *name() const noexcept override;
        [[nodiscard]] std::string message(int value) const override;
        [[nodiscard]] std::error_condition default_error_condition(int value) const noexcept override;
    };

    std::error_code make_error_code(TimeoutError e);

    template<typename T, typename E>
        requires (!std::is_same_v<E, std::exception_ptr>)
    zero::async::coroutine::Task<tl::expected<T, E>, TimeoutError>
    timeout(zero::async::coroutine::Task<T, E> task, const std::chrono::milliseconds ms) {
        if (ms == std::chrono::milliseconds::zero())
            co_return tl::expected<tl::expected<T, E>, TimeoutError>{co_await task};

        auto timer = sleep(ms);
        auto future = timer.future().then([&] {
            return task.cancel();
        });

        auto result = co_await task;

        if (future.isReady()) {
            if (!future.result())
                co_return tl::expected<tl::expected<T, E>, TimeoutError>{std::move(result)};

            co_return tl::unexpected(TimeoutError::ELAPSED);
        }

        timer.cancel();
        co_await std::move(future);

        co_return tl::expected<tl::expected<T, E>, TimeoutError>{std::move(result)};
    }

    template<typename F, typename T = std::invoke_result_t<F>>
        requires zero::detail::is_specialization<T, zero::async::coroutine::Task>
    tl::expected<tl::expected<typename T::value_type, typename T::error_type>, std::error_code> run(F &&f) {
        const auto eventLoop = EventLoop::make().transform([](EventLoop &&e) {
            return std::make_shared<EventLoop>(std::move(e));
        });
        EXPECT(eventLoop);

        setEventLoop(*eventLoop);

        auto future = f().future().finally([&] {
            eventLoop.value()->loopExit();
        });

        eventLoop.value()->dispatch();
        assert(future.isReady());
        return {std::move(future).result()};
    }
}

template<>
struct std::is_error_code_enum<asyncio::EventLoop::Error> : std::true_type {
};

template<>
struct std::is_error_code_enum<asyncio::TimeoutError> : std::true_type {
};

#endif //ASYNCIO_EVENT_LOOP_H

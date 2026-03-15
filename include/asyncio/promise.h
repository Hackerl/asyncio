#ifndef ASYNCIO_PROMISE_H
#define ASYNCIO_PROMISE_H

#include "event_loop.h"
#include <zero/async/promise.h>

namespace asyncio {
    template<typename T, typename E = std::nullptr_t>
    class Promise : public zero::async::promise::Promise<T, E> {
    public:
        Promise() : mEventLoop{getEventLoop()} {
        }

        explicit Promise(std::shared_ptr<EventLoop> eventLoop) : mEventLoop{std::move(eventLoop)} {
        }

        explicit Promise(zero::async::promise::Promise<T, E> &&rhs)
            : zero::async::promise::Promise<T, E>{std::move(rhs)}, mEventLoop{getEventLoop()} {
        }

        Promise(zero::async::promise::Promise<T, E> &&rhs, std::shared_ptr<EventLoop> eventLoop)
            : zero::async::promise::Promise<T, E>{std::move(rhs)}, mEventLoop{std::move(eventLoop)} {
        }

        template<typename... Args>
        void resolve(Args &&... args) {
            assert(this->mCore);
            assert(!this->mCore->result);
            assert(this->mCore->state != zero::async::promise::State::OnlyResult);
            assert(this->mCore->state != zero::async::promise::State::Done);

            if constexpr (std::is_void_v<T>)
                this->mCore->result.emplace();
            else
                this->mCore->result.emplace(std::in_place, std::forward<Args>(args)...);

            auto state = this->mCore->state.load();

            if (state == zero::async::promise::State::Pending &&
                this->mCore->state.compare_exchange_strong(state, zero::async::promise::State::OnlyResult)) {
                this->mCore->event.set();
                return;
            }

            if (state != zero::async::promise::State::OnlyCallback ||
                !this->mCore->state.compare_exchange_strong(state, zero::async::promise::State::Done))
                throw zero::error::StacktraceError<std::logic_error>{
                    fmt::format("Unexpected promise state: {}", std::to_underlying(state))
                };

            this->mCore->event.set();

            mEventLoop->post([core = this->mCore] {
                core->trigger();
            });
        }

        template<typename... Args>
        void reject(Args &&... args) {
            assert(this->mCore);
            assert(!this->mCore->result);
            assert(this->mCore->state != zero::async::promise::State::OnlyResult);
            assert(this->mCore->state != zero::async::promise::State::Done);

            this->mCore->result.emplace(std::unexpected<E>(std::in_place, std::forward<Args>(args)...));
            auto state = this->mCore->state.load();

            if (state == zero::async::promise::State::Pending &&
                this->mCore->state.compare_exchange_strong(state, zero::async::promise::State::OnlyResult)) {
                this->mCore->event.set();
                return;
            }

            if (state != zero::async::promise::State::OnlyCallback ||
                !this->mCore->state.compare_exchange_strong(state, zero::async::promise::State::Done))
                throw zero::error::StacktraceError<std::logic_error>{
                    fmt::format("Unexpected promise state: {}", std::to_underlying(state))
                };

            this->mCore->event.set();

            mEventLoop->post([core = this->mCore] {
                core->trigger();
            });
        }

    private:
        std::shared_ptr<EventLoop> mEventLoop;
    };
}

#endif //ASYNCIO_PROMISE_H

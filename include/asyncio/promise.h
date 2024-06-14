#ifndef ASYNCIO_PROMISE_H
#define ASYNCIO_PROMISE_H

#include "event_loop.h"

namespace asyncio {
    template<typename T, typename E = std::nullptr_t>
    class Promise : public zero::async::promise::Promise<T, E> {
    public:
        Promise() : mEventLoop(getEventLoop()) {
        }

        explicit Promise(std::shared_ptr<EventLoop> eventLoop): mEventLoop(std::move(eventLoop)) {
        }

        explicit Promise(zero::async::promise::Promise<T, E> &&rhs)
            : zero::async::promise::Promise<T, E>(std::move(rhs)), mEventLoop(getEventLoop()) {
        }

        Promise(zero::async::promise::Promise<T, E> &&rhs, std::shared_ptr<EventLoop> eventLoop)
            : zero::async::promise::Promise<T, E>(std::move(rhs)), mEventLoop(std::move(eventLoop)) {
        }

        template<typename... Ts>
        void resolve(Ts &&... args) {
            assert(this->mCore);
            assert(!this->mCore->result);
            assert(this->mCore->state != zero::async::promise::State::ONLY_RESULT);
            assert(this->mCore->state != zero::async::promise::State::DONE);

            if constexpr (std::is_void_v<T>)
                this->mCore->result.emplace();
            else
                this->mCore->result.emplace(std::in_place, std::forward<Ts>(args)...);

            zero::async::promise::State state = this->mCore->state;

            if (state == zero::async::promise::State::PENDING &&
                this->mCore->state.compare_exchange_strong(state, zero::async::promise::State::ONLY_RESULT)) {
                this->mCore->event.set();
                return;
            }

            if (state != zero::async::promise::State::ONLY_CALLBACK ||
                !this->mCore->state.compare_exchange_strong(state, zero::async::promise::State::DONE))
                throw std::logic_error(fmt::format("unexpected state: {}", static_cast<int>(state)));

            this->mCore->event.set();

            const auto result = mEventLoop->post([core = this->mCore] {
                core->trigger();
            });
            assert(result);
        }

        template<typename... Ts>
        void reject(Ts &&... args) {
            assert(this->mCore);
            assert(!this->mCore->result);
            assert(this->mCore->state != zero::async::promise::State::ONLY_RESULT);
            assert(this->mCore->state != zero::async::promise::State::DONE);

            this->mCore->result.emplace(std::unexpected<E>(std::in_place, std::forward<Ts>(args)...));
            zero::async::promise::State state = this->mCore->state;

            if (state == zero::async::promise::State::PENDING &&
                this->mCore->state.compare_exchange_strong(state, zero::async::promise::State::ONLY_RESULT)) {
                this->mCore->event.set();
                return;
            }

            if (state != zero::async::promise::State::ONLY_CALLBACK ||
                !this->mCore->state.compare_exchange_strong(state, zero::async::promise::State::DONE))
                throw std::logic_error(fmt::format("unexpected state: {}", static_cast<int>(state)));

            this->mCore->event.set();

            const auto result = mEventLoop->post([core = this->mCore] {
                core->trigger();
            });
            assert(result);
        }

    private:
        std::shared_ptr<EventLoop> mEventLoop;
    };
}

#endif //ASYNCIO_PROMISE_H

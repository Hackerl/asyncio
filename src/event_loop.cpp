#include <asyncio/event_loop.h>
#include <asyncio/ev/timer.h>
#include <event2/dns.h>
#include <event2/thread.h>
#include <zero/defer.h>
#include <mutex>

thread_local std::weak_ptr<asyncio::EventLoop> threadEventLoop;

asyncio::EventLoop::EventLoop(event_base *base, evdns_base *dnsBase, const std::size_t maxWorkers)
    : mMaxWorkers(maxWorkers), mBase(base, event_base_free),
      mDnsBase(dnsBase, [](evdns_base *b) { evdns_base_free(b, 0); }) {
}

event_base *asyncio::EventLoop::base() const {
    return mBase.get();
}

evdns_base *asyncio::EventLoop::dnsBase() const {
    return mDnsBase.get();
}

bool asyncio::EventLoop::addNameserver(const char *ip) const {
    return evdns_base_nameserver_ip_add(mDnsBase.get(), ip) == 0;
}

void asyncio::EventLoop::dispatch() const {
    event_base_dispatch(mBase.get());
}

void asyncio::EventLoop::loopBreak() const {
    event_base_loopbreak(mBase.get());
}

void asyncio::EventLoop::loopExit(const std::optional<std::chrono::milliseconds> ms) const {
    if (!ms) {
        event_base_loopexit(mBase.get(), nullptr);
        return;
    }

    const timeval tv = {
        static_cast<decltype(timeval::tv_sec)>(ms->count() / 1000),
        static_cast<decltype(timeval::tv_usec)>(ms->count() % 1000 * 1000)
    };

    event_base_loopexit(mBase.get(), &tv);
}

std::shared_ptr<asyncio::EventLoop> asyncio::getEventLoop() {
    if (threadEventLoop.expired())
        return nullptr;

    return threadEventLoop.lock();
}

void asyncio::setEventLoop(const std::weak_ptr<EventLoop> &eventLoop) {
    threadEventLoop = eventLoop;
}

tl::expected<asyncio::EventLoop, std::error_code> asyncio::createEventLoop(const std::size_t maxWorkers) {
    static std::once_flag flag;

    std::call_once(flag, [] {
#ifdef _WIN32
        evthread_use_windows_threads();
#else
        evthread_use_pthreads();
#endif

        std::atexit([] {
            libevent_global_shutdown();
        });
    });

    event_config *cfg = event_config_new();

    if (!cfg)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

    DEFER(event_config_free(cfg));
    event_config_set_flag(cfg, EVENT_BASE_FLAG_DISALLOW_SIGNALFD);

    event_base *base = event_base_new_with_config(cfg);

    if (!base)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

#ifdef __ANDROID__
    evdns_base *dnsBase = evdns_base_new(base, 0);

#ifndef NO_DEFAULT_NAMESERVER
#ifndef DEFAULT_NAMESERVER
#define DEFAULT_NAMESERVER "8.8.8.8"
#endif
    if (evdns_base_nameserver_ip_add(dnsBase, DEFAULT_NAMESERVER) != 0) {
        event_base_free(base);
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }
#endif
#else
    evdns_base *dnsBase = evdns_base_new(base, EVDNS_BASE_INITIALIZE_NAMESERVERS);
#endif

    if (!dnsBase) {
        event_base_free(base);
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }

    return EventLoop{base, dnsBase, maxWorkers};
}

zero::async::coroutine::Task<void, std::error_code> asyncio::sleep(const std::chrono::milliseconds ms) {
    auto timer = CO_TRY(ev::makeTimer());
    co_return co_await timer->after(ms);
}

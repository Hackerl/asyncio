#include <asyncio/event_loop.h>
#include <asyncio/ev/timer.h>
#include <event2/dns.h>
#include <event2/thread.h>

thread_local std::weak_ptr<asyncio::EventLoop> threadEventLoop;

asyncio::EventLoop::EventLoop(event_base *base, evdns_base *dnsBase, size_t maxWorkers)
        : mMaxWorkers(maxWorkers), mBase(base, event_base_free),
          mDnsBase(dnsBase, [](evdns_base *base) { evdns_base_free(base, 0); }) {

}

event_base *asyncio::EventLoop::base() {
    return mBase.get();
}

evdns_base *asyncio::EventLoop::dnsBase() {
    return mDnsBase.get();
}

bool asyncio::EventLoop::addNameserver(const char *ip) {
    return evdns_base_nameserver_ip_add(mDnsBase.get(), ip) == 0;
}

void asyncio::EventLoop::dispatch() {
    event_base_dispatch(mBase.get());
}

void asyncio::EventLoop::loopBreak() {
    event_base_loopbreak(mBase.get());
}

void asyncio::EventLoop::loopExit(std::optional<std::chrono::milliseconds> ms) {
    if (!ms) {
        event_base_loopexit(mBase.get(), nullptr);
        return;
    }

    timeval tv = {
            (decltype(timeval::tv_sec)) (ms->count() / 1000),
            (decltype(timeval::tv_usec)) ((ms->count() % 1000) * 1000)
    };

    event_base_loopexit(mBase.get(), &tv);
}

std::shared_ptr<asyncio::EventLoop> asyncio::getEventLoop() {
    if (threadEventLoop.expired())
        return nullptr;

    return threadEventLoop.lock();
}

bool asyncio::setEventLoop(const std::weak_ptr<EventLoop> &eventLoop) {
    if (!threadEventLoop.expired())
        return false;

    threadEventLoop = eventLoop;
    return true;
}

tl::expected<asyncio::EventLoop, std::error_code> asyncio::createEventLoop(size_t maxWorkers) {
    static std::once_flag flag;

    std::call_once(flag, []() {
#ifdef _WIN32
        evthread_use_windows_threads();
#else
        evthread_use_pthreads();
#endif

        std::atexit([]() {
            libevent_global_shutdown();
        });
    });

    event_base *base = event_base_new();

    if (!base)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

#if _WIN32 || __APPLE__ || __linux__ && !__ANDROID__
    evdns_base *dnsBase = evdns_base_new(base, EVDNS_BASE_INITIALIZE_NAMESERVERS);
#else
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
#endif

    if (!dnsBase) {
        event_base_free(base);
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
    }

    return EventLoop{base, dnsBase, maxWorkers};
}

zero::async::coroutine::Task<void, std::error_code> asyncio::sleep(std::chrono::milliseconds ms) {
    auto timer = ev::makeTimer();

    if (!timer)
        co_return tl::unexpected(timer.error());

    co_return co_await timer->after(ms);
}

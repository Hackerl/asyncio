#include <asyncio/event_loop.h>
#include <event2/dns.h>
#include <event2/thread.h>

thread_local std::weak_ptr<asyncio::EventLoop> threadEventLoop;

asyncio::EventLoop::EventLoop(event_base *base, evdns_base *dnsBase, size_t maxWorkers)
        : mBase(base), mDnsBase(dnsBase), mMaxWorkers(maxWorkers) {

}

asyncio::EventLoop::~EventLoop() {
    evdns_base_free(mDnsBase, 0);
    event_base_free(mBase);
}

event_base *asyncio::EventLoop::base() {
    return mBase;
}

evdns_base *asyncio::EventLoop::dnsBase() {
    return mDnsBase;
}

bool asyncio::EventLoop::addNameserver(const char *ip) {
    return evdns_base_nameserver_ip_add(mDnsBase, ip) == 0;
}

void asyncio::EventLoop::dispatch() {
    event_base_dispatch(mBase);
}

void asyncio::EventLoop::loopBreak() {
    event_base_loopbreak(mBase);
}

void asyncio::EventLoop::loopExit(std::optional<std::chrono::milliseconds> ms) {
    if (!ms) {
        event_base_loopexit(mBase, nullptr);
        return;
    }

    timeval tv = {
            (time_t) (ms->count() / 1000),
            (suseconds_t) ((ms->count() % 1000) * 1000)
    };

    event_base_loopexit(mBase, &tv);
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

tl::expected<std::shared_ptr<asyncio::EventLoop>, std::error_code> asyncio::newEventLoop(size_t maxWorkers) {
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

#if _WIN32 || __linux__ && !__ANDROID__
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

    return std::make_shared<EventLoop>(base, dnsBase, maxWorkers);
}

zero::async::coroutine::Task<void> asyncio::sleep(std::chrono::milliseconds ms) {
    zero::async::promise::Promise<void, nullptr_t> promise;

    getEventLoop()->post(
            [=]() mutable {
                promise.resolve();
            },
            ms
    );

    co_await promise;
}

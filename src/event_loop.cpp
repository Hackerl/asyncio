#include <event.h>
#include <asyncio/event_loop.h>
#include <asyncio/ev/timer.h>
#include <event2/dns.h>
#include <event2/thread.h>
#include <zero/defer.h>
#include <mutex>

#ifdef _WIN32
#include <asyncio/fs/iocp.h>
#elif __APPLE__ || (__unix__ && !__ANDROID__)
#include <asyncio/fs/posix.h>
#endif

thread_local std::weak_ptr<asyncio::EventLoop> threadEventLoop;

asyncio::EventLoop::EventLoop(
    std::unique_ptr<event_base, decltype(event_base_free) *> base,
    std::unique_ptr<evdns_base, void (*)(evdns_base *)> dnsBase,
    std::unique_ptr<fs::IFramework> framework,
    const std::size_t maxWorkers
) : mMaxWorkers(maxWorkers), mBase(std::move(base)), mDnsBase(std::move(dnsBase)), mFramework(std::move(framework)) {
}

event_base *asyncio::EventLoop::base() const {
    return mBase.get();
}

evdns_base *asyncio::EventLoop::dnsBase() const {
    return mDnsBase.get();
}

asyncio::fs::IFramework * asyncio::EventLoop::framework() const {
    return mFramework.get();
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

    auto base = std::unique_ptr<event_base, decltype(event_base_free) *>(
        event_base_new_with_config(cfg),
        event_base_free
    );

    if (!base)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

#ifdef __ANDROID__
    auto dnsBase = std::unique_ptr<evdns_base, void (*)(evdns_base *)>(
        evdns_base_new(base.get(), 0),
        [](evdns_base *b) {
            evdns_base_free(b, 0);
        }
    );

    if (!dnsBase)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));

#ifndef NO_DEFAULT_NAMESERVER
#ifndef DEFAULT_NAMESERVER
#define DEFAULT_NAMESERVER "8.8.8.8"
#endif
    if (evdns_base_nameserver_ip_add(dnsBase.get(), DEFAULT_NAMESERVER) != 0)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
#endif
#else
    auto dnsBase = std::unique_ptr<evdns_base, void (*)(evdns_base *)>(
        evdns_base_new(base.get(), EVDNS_BASE_INITIALIZE_NAMESERVERS),
        [](evdns_base *b) {
            evdns_base_free(b, 0);
        }
    );

    if (!dnsBase)
        return tl::unexpected(std::error_code(EVUTIL_SOCKET_ERROR(), std::system_category()));
#endif

#ifdef __ANDROID__
    return EventLoop{std::move(base), std::move(dnsBase), nullptr, maxWorkers};
#else
#ifdef _WIN32
    auto framework = TRY(fs::makeIOCP().transform([](fs::IOCP &&iocp) {
        return std::make_unique<fs::IOCP>(std::move(iocp));
    }));
#elif __APPLE__ || (__unix__ && !__ANDROID__)
    auto framework = fs::makePosixAIO(base.get()).transform([](fs::PosixAIO &&aio) {
        return std::make_unique<fs::PosixAIO>(std::move(aio));
    });
#endif

    return EventLoop{std::move(base), std::move(dnsBase), std::move(*framework), maxWorkers};
#endif
}

zero::async::coroutine::Task<void, std::error_code> asyncio::sleep(const std::chrono::milliseconds ms) {
    auto timer = CO_TRY(ev::makeTimer());
    co_return co_await timer->after(ms);
}

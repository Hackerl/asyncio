#include <event.h>
#include <asyncio/event_loop.h>
#include <asyncio/ev/timer.h>
#include <event2/dns.h>
#include <event2/thread.h>
#include <zero/defer.h>
#include <mutex>

#ifdef _WIN32
#include <asyncio/fs/iocp.h>
#endif

#if __linux__
#include <asyncio/fs/aio.h>
#endif

#if __APPLE__ || (__unix__ && !__ANDROID__)
#include <asyncio/fs/posix.h>
#endif

thread_local std::weak_ptr<asyncio::EventLoop> threadEventLoop;

asyncio::EventLoop::EventLoop(
    std::unique_ptr<event_base, decltype(event_base_free) *> base,
    std::unique_ptr<fs::IFramework> framework,
    const std::size_t maxWorkers
) : mMaxWorkers(maxWorkers), mBase(std::move(base)), mFramework(std::move(framework)) {
}

tl::expected<asyncio::EventLoop, std::error_code> asyncio::EventLoop::make(const std::size_t maxWorkers) {
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
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

    DEFER(event_config_free(cfg));
    event_config_set_flag(cfg, EVENT_BASE_FLAG_DISALLOW_SIGNALFD);

    std::unique_ptr<event_base, decltype(event_base_free) *> base(
        event_base_new_with_config(cfg),
        event_base_free
    );

    if (!base)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

#ifdef _WIN32
    auto framework = fs::IOCP::make().transform([](fs::IOCP &&iocp) {
        return std::make_unique<fs::IOCP>(std::move(iocp));
    });
#elif __ANDROID__
    auto framework = fs::AIO::make(base.get()).transform([](fs::AIO &&aio) {
        return std::make_unique<fs::AIO>(std::move(aio));
    });
#elif __linux__
    auto framework = fs::AIO::make(base.get())
                     .transform([](fs::AIO &&aio) -> std::unique_ptr<fs::IFramework> {
                         return std::make_unique<fs::AIO>(std::move(aio));
                     })
                     .or_else([&](const auto &) {
                         return fs::PosixAIO::make(base.get())
                             .transform([](fs::PosixAIO &&aio) -> std::unique_ptr<fs::IFramework> {
                                 return std::make_unique<fs::PosixAIO>(std::move(aio));
                             });
                     });
#elif __APPLE__
    auto framework = fs::PosixAIO::make(base.get()).transform([](fs::PosixAIO &&aio) {
        return std::make_unique<fs::PosixAIO>(std::move(aio));
    });
#endif
    EXPECT(framework);

    return EventLoop{std::move(base), *std::move(framework), maxWorkers};
}

event_base *asyncio::EventLoop::base() {
    return mBase.get();
}

const event_base *asyncio::EventLoop::base() const {
    return mBase.get();
}

tl::expected<void, std::error_code> asyncio::EventLoop::addNameserver(const char *ip) {
    auto address = net::addressFrom(ip, 0);
    EXPECT(address);
    mNameservers.push_back(*std::move(address));
    return {};
}

asyncio::fs::IFramework &asyncio::EventLoop::framework() {
    return *mFramework;
}

const asyncio::fs::IFramework &asyncio::EventLoop::framework() const {
    return *mFramework;
}

tl::expected<std::unique_ptr<evdns_base, void (*)(evdns_base *)>, std::error_code>
asyncio::EventLoop::makeDNSBase() const {
#ifdef __ANDROID__
    std::unique_ptr<evdns_base, void (*)(evdns_base *)> dnsBase(
        evdns_base_new(mBase.get(), 0),
        [](evdns_base *b) {
            evdns_base_free(b, 1);
        }
    );

    if (!dnsBase)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());

#ifndef NO_DEFAULT_NAMESERVER
#ifndef DEFAULT_NAMESERVER
#define DEFAULT_NAMESERVER "8.8.8.8"
#endif
    if (evdns_base_nameserver_ip_add(dnsBase.get(), DEFAULT_NAMESERVER) != 0)
        return tl::unexpected(Error::INVALID_NAMESERVER);
#endif
#else
    std::unique_ptr<evdns_base, void (*)(evdns_base *)> dnsBase(
        evdns_base_new(mBase.get(), EVDNS_BASE_INITIALIZE_NAMESERVERS),
        [](evdns_base *b) {
            evdns_base_free(b, 1);
        }
    );

    if (!dnsBase)
        return tl::unexpected<std::error_code>(EVUTIL_SOCKET_ERROR(), std::system_category());
#endif

    for (const auto &server: mNameservers) {
        const auto result = socketAddressFrom(server);
        EXPECT(result);

        if (evdns_base_nameserver_sockaddr_add(dnsBase.get(), result->first.get(), result->second, 0) != 0)
            return tl::unexpected(Error::INVALID_NAMESERVER);
    }

    return dnsBase;
}

void asyncio::EventLoop::dispatch() const {
    event_base_loop(mBase.get(), EVLOOP_NO_EXIT_ON_EMPTY);
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

zero::async::coroutine::Task<void, std::error_code> asyncio::sleep(const std::chrono::milliseconds ms) {
    auto timer = ev::Timer::make();
    CO_EXPECT(timer);
    co_return co_await timer->after(ms);
}

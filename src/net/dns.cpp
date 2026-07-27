#include <asyncio/net/dns.h>
#include <zero/defer.h>

#ifdef ASYNCIO_ENABLE_C_ARES
#include <ares.h>

namespace asyncio::net::dns {
    Z_DEFINE_ERROR_TRANSFORMER_EX(
        ARESError,
        "asyncio::net::dns::ares",
        ares_strerror,
        [](const int value) -> std::optional<std::error_condition> {
            switch (value) {
                case ARES_ENOMEM:
                    return std::errc::not_enough_memory;

                case ARES_EBADQUERY:
                case ARES_EBADNAME:
                case ARES_EBADFAMILY:
                case ARES_EBADRESP:
                case ARES_EBADSTR:
                case ARES_EBADHINTS:
                case ARES_EBADFLAGS:
                case ARES_ENONAME:
                case ARES_ESERVICE:
                    return std::errc::invalid_argument;

                case ARES_ENOTFOUND:
                    return std::errc::no_such_device_or_address;

                case ARES_EREFUSED:
                    return std::errc::permission_denied;

                case ARES_ECONNREFUSED:
                    return std::errc::connection_refused;

                case ARES_ENOTIMP:
                    return std::errc::function_not_supported;

                case ARES_ETIMEOUT:
                    return std::errc::timed_out;

                case ARES_ECANCELLED:
                case ARES_EDESTRUCTION:
                    return std::errc::operation_canceled;

                case ARES_EFILE:
                    return std::errc::io_error;

                default:
                    return std::nullopt;
            }
        }
    )
}

Z_DECLARE_ERROR_CODE(asyncio::net::dns::ARESError)

Z_DEFINE_ERROR_CATEGORY_INSTANCE(asyncio::net::dns::ARESError)

namespace asyncio::net::dns {
    template<typename F>
        requires requires(F &&f) {
            { std::invoke(std::forward<F>(f)) } -> std::same_as<int>;
        }
    std::expected<std::invoke_result_t<F>, std::error_code> expected(F &&f) {
        const auto result = std::invoke(std::forward<F>(f));

        if (result != ARES_SUCCESS)
            return std::unexpected{static_cast<ARESError>(result)};

        return result;
    }

    struct Resolver::Core {
        struct Context {
            uv::Handle<uv_poll_t> poll;
            Core *core{};
            ares_socket_t socket;
        };

        uv::Handle<uv_timer_t> timer;
        std::map<ares_socket_t, std::unique_ptr<Context>> contexts;
        std::unique_ptr<ares_channel_t, decltype(&ares_destroy)> channel{nullptr, ares_destroy};

        void updateTimer();
        void updatePoll(ares_socket_t socket, int readable, int writable);
    };
}

void asyncio::net::dns::Resolver::Core::updateTimer() {
    timeval tv{};

    if (const auto *ptr = ares_timeout(channel.get(), nullptr, &tv)) {
        zero::error::guard(uv::expected([&] {
            return uv_timer_start(
                timer.raw(),
                [](auto *handle) {
                    auto &core = *static_cast<Core *>(handle->data);
                    ares_process_fd(core.channel.get(), ARES_SOCKET_BAD, ARES_SOCKET_BAD);
                    core.updateTimer();
                },
                ptr->tv_sec * 1000 + ptr->tv_usec / 1000,
                0
            );
        }));
    }
    else {
        zero::error::guard(uv::expected([&] {
            return uv_timer_stop(timer.raw());
        }));
    }
}

void asyncio::net::dns::Resolver::Core::updatePoll(const ares_socket_t socket, const int readable, const int writable) {
    if (!readable && !writable) {
        if (const auto it = contexts.find(socket); it != contexts.end()) {
            zero::error::guard(uv::expected([&] {
                return uv_poll_stop(it->second->poll.raw());
            }));

            contexts.erase(it);
        }

        return;
    }

    uv_poll_t *ptr{};

    if (const auto it = contexts.find(socket); it != contexts.end()) {
        ptr = it->second->poll.raw();
    }
    else {
        auto poll = std::make_unique<uv_poll_t>();

        zero::error::guard(uv::expected([&] {
            return uv_poll_init_socket(getEventLoop()->raw(), poll.get(), socket);
        }));

        ptr = poll.get();

        auto context = std::make_unique<Context>(uv::Handle{std::move(poll)}, this, socket);
        context->poll->data = context.get();

        contexts.emplace(socket, std::move(context));
    }

    zero::error::guard(uv::expected([&] {
        return uv_poll_start(
            ptr,
            (readable ? UV_READABLE : 0) | (writable ? UV_WRITABLE : 0),
            [](auto *handle, const int status, const int events) {
                const auto context = static_cast<Context *>(handle->data);

                // Copy: ares_process_fd may trigger updatePoll which deletes the context.
                const auto core = context->core;

                if (status < 0) {
                    ares_process_fd(core->channel.get(), ARES_SOCKET_BAD, ARES_SOCKET_BAD);
                    core->updateTimer();
                    return;
                }

                ares_process_fd(
                    core->channel.get(),
                    events & UV_READABLE ? context->socket : ARES_SOCKET_BAD,
                    events & UV_WRITABLE ? context->socket : ARES_SOCKET_BAD
                );

                core->updateTimer();
            }
        );
    }));
}

asyncio::net::dns::Resolver::Resolver(std::unique_ptr<Core> core) : mCore{std::move(core)} {
}

asyncio::net::dns::Resolver::Resolver(Resolver &&) noexcept = default;
asyncio::net::dns::Resolver &asyncio::net::dns::Resolver::operator=(Resolver &&) noexcept = default;
asyncio::net::dns::Resolver::~Resolver() = default;

asyncio::net::dns::Resolver asyncio::net::dns::Resolver::make() {
    static std::once_flag flag;

    std::call_once(
        flag,
        [] {
            zero::error::guard(expected([] {
                return ares_library_init(ARES_LIB_INIT_ALL);;
            }));

            std::atexit([] {
                ares_library_cleanup();
            });
        }
    );

    auto timer = std::make_unique<uv_timer_t>();

    zero::error::guard(uv::expected([&] {
        return uv_timer_init(getEventLoop()->raw(), timer.get());
    }));

    auto core = std::make_unique<Core>(uv::Handle{std::move(timer)});
    core->timer->data = core.get();

    const ares_options options{
        .sock_state_cb = [](void *data, const ares_socket_t socket, const int readable, const int writable) {
            static_cast<Core *>(data)->updatePoll(socket, readable, writable);
        },
        .sock_state_cb_data = core.get(),
    };

    ares_channel_t *channel{};

    zero::error::guard(expected([&] {
        return ares_init_options(&channel, &options, ARES_OPT_SOCK_STATE_CB);
    }));

    core->channel.reset(channel);
    return Resolver{std::move(core)};
}

asyncio::net::dns::Resolver &asyncio::net::dns::Resolver::current() {
    thread_local std::optional<Resolver> resolver;

    if (!resolver) {
        resolver = make();
        getEventLoop()->onDestroy([] {
            resolver.reset();
        });
    }

    return *resolver;
}

const std::vector<std::string> &asyncio::net::dns::Resolver::getServers() const {
    return mServers;
}

std::expected<void, std::error_code> asyncio::net::dns::Resolver::setServers(std::vector<std::string> servers) {
    Z_EXPECT(expected([&] {
        return ares_set_servers_csv(
            mCore->channel.get(),
            to_string(fmt::join(servers, ",")).c_str()
        );
    }));

    mServers = std::move(servers);
    return {};
}

// ReSharper disable once CppMemberFunctionMayBeConst
void asyncio::net::dns::Resolver::cancelAll() {
    ares_cancel(mCore->channel.get());
}

// ReSharper disable once CppMemberFunctionMayBeConst
asyncio::task::Task<std::vector<asyncio::net::Address>, std::error_code>
asyncio::net::dns::Resolver::getAddressInfo(
    const std::string node,
    const std::optional<std::string> service,
    const std::optional<addrinfo> hints
) {
    Promise<std::vector<Address>, ARESError> promise;

    const auto aresHints = hints.transform([](const auto &value) {
        return ares_addrinfo_hints{value.ai_flags, value.ai_family, value.ai_socktype, value.ai_protocol};
    });

    ares_getaddrinfo(
        mCore->channel.get(),
        node.c_str(),
        service ? service->c_str() : nullptr,
        aresHints ? &*aresHints : nullptr,
        [](void *arg, const int status, int, ares_addrinfo *result) {
            const auto p = static_cast<Promise<std::vector<Address>, ARESError> *>(arg);

            if (status != ARES_SUCCESS) {
                p->reject(static_cast<ARESError>(status));
                return;
            }

            Z_DEFER(ares_freeaddrinfo(result));
            std::vector<Address> addresses;

            for (const auto *ptr = result->nodes; ptr; ptr = ptr->ai_next) {
                auto address = addressFrom(ptr->ai_addr, ptr->ai_addrlen);

                if (!address)
                    continue;

                addresses.push_back(*std::move(address));
            }

            std::ranges::sort(addresses);
            const auto [first, last] = std::ranges::unique(addresses);
            addresses.erase(first, last);

            p->resolve(std::move(addresses));
        },
        &promise
    );

    mCore->updateTimer();
    co_return co_await promise.getFuture();
}

asyncio::task::Task<std::vector<asyncio::net::IP>, std::error_code>
asyncio::net::dns::Resolver::lookupIP(std::string host) {
    co_return co_await getAddressInfo(
        std::move(host),
        std::nullopt,
        addrinfo{
            .ai_flags = AI_ADDRCONFIG,
            .ai_family = AF_UNSPEC
        }
    ).transform([](const auto &addresses) {
        return addresses
            | std::views::transform(
                [](const auto &address) -> IP {
                    if (const auto addr = std::get_if<IPv4Address>(&address))
                        return addr->ip;

                    return std::get<IPv6Address>(address).ip;
                }
            )
            | std::ranges::to<std::vector>();
    });
}

asyncio::task::Task<std::vector<asyncio::net::IPv4>, std::error_code>
asyncio::net::dns::Resolver::lookupIPv4(std::string host) {
    co_return co_await getAddressInfo(
        std::move(host),
        std::nullopt,
        addrinfo{
            .ai_flags = AI_ADDRCONFIG,
            .ai_family = AF_INET
        }
    ).transform([](const auto &addresses) {
        return addresses
            | std::views::transform(
                [](const auto &address) {
                    return std::get<IPv4Address>(address).ip;
                }
            )
            | std::ranges::to<std::vector>();
    });
}

asyncio::task::Task<std::vector<asyncio::net::IPv6>, std::error_code>
asyncio::net::dns::Resolver::lookupIPv6(std::string host) {
    co_return co_await getAddressInfo(
        std::move(host),
        std::nullopt,
        addrinfo{
            .ai_flags = AI_ADDRCONFIG,
            .ai_family = AF_INET6
        }
    ).transform([](std::span<const Address> addresses) {
        return addresses
            | std::views::transform(
                [](const auto &address) {
                    return std::get<IPv6Address>(address).ip;
                }
            )
            | std::ranges::to<std::vector>();
    });
}

const std::vector<std::string> &asyncio::net::dns::getServers() {
    return Resolver::current().getServers();
}

std::expected<void, std::error_code> asyncio::net::dns::setServers(std::vector<std::string> servers) {
    return Resolver::current().setServers(std::move(servers));
}
#endif

asyncio::task::Task<std::vector<asyncio::net::Address>, std::error_code>
asyncio::net::dns::getAddressInfo(
    std::string node,
    std::optional<std::string> service,
    const std::optional<addrinfo> hints
) {
#ifdef ASYNCIO_ENABLE_C_ARES
    co_return co_await Resolver::current().getAddressInfo(std::move(node), std::move(service), hints);
#else
    Promise<std::vector<Address>, uv::Error> promise;
    uv_getaddrinfo_t request{.data = &promise};

    Z_CO_EXPECT(uv::expected([&] {
        return uv_getaddrinfo(
            getEventLoop()->raw(),
            &request,
            [](auto *req, const int status, addrinfo *result) {
                const auto p = static_cast<Promise<std::vector<Address>, uv::Error> *>(req->data);

                if (status < 0) {
                    p->reject(static_cast<uv::Error>(status));
                    return;
                }

                Z_DEFER(uv_freeaddrinfo(result));
                std::vector<Address> addresses;

                for (const auto *ptr = result; ptr; ptr = ptr->ai_next) {
                    auto address = addressFrom(ptr->ai_addr, static_cast<socklen_t>(ptr->ai_addrlen));

                    if (!address)
                        continue;

                    addresses.push_back(*std::move(address));
                }

                std::ranges::sort(addresses);
                const auto [first, last] = std::ranges::unique(addresses);
                addresses.erase(first, last);

                p->resolve(std::move(addresses));
            },
            node.c_str(),
            service ? service->c_str() : nullptr,
            hints ? &*hints : nullptr
        );
    }));

    co_return co_await task::Cancellable{
        promise.getFuture(),
        [&]() -> std::expected<void, std::error_code> {
            Z_EXPECT(uv::expected([&] {
                return uv_cancel(reinterpret_cast<uv_req_t *>(&request));
            }));
            return {};
        }
    };
#endif
}

asyncio::task::Task<std::vector<asyncio::net::IP>, std::error_code>
asyncio::net::dns::lookupIP(std::string host) {
    co_return co_await getAddressInfo(
        std::move(host),
        std::nullopt,
        addrinfo{
            .ai_flags = AI_ADDRCONFIG,
            .ai_family = AF_UNSPEC
        }
    ).transform([](const auto &addresses) {
        return addresses
            | std::views::transform(
                [](const auto &address) -> IP {
                    if (const auto addr = std::get_if<IPv4Address>(&address))
                        return addr->ip;

                    return std::get<IPv6Address>(address).ip;
                }
            )
            | std::ranges::to<std::vector>();
    });
}

asyncio::task::Task<std::vector<asyncio::net::IPv4>, std::error_code>
asyncio::net::dns::lookupIPv4(std::string host) {
    co_return co_await getAddressInfo(
        std::move(host),
        std::nullopt,
        addrinfo{
            .ai_flags = AI_ADDRCONFIG,
            .ai_family = AF_INET
        }
    ).transform([](const auto &addresses) {
        return addresses
            | std::views::transform(
                [](const auto &address) {
                    return std::get<IPv4Address>(address).ip;
                }
            )
            | std::ranges::to<std::vector>();
    });
}

asyncio::task::Task<std::vector<asyncio::net::IPv6>, std::error_code>
asyncio::net::dns::lookupIPv6(std::string host) {
    co_return co_await getAddressInfo(
        std::move(host),
        std::nullopt,
        addrinfo{
            .ai_flags = AI_ADDRCONFIG,
            .ai_family = AF_INET6
        }
    ).transform([](std::span<const Address> addresses) {
        return addresses
            | std::views::transform(
                [](const auto &address) {
                    return std::get<IPv6Address>(address).ip;
                }
            )
            | std::ranges::to<std::vector>();
    });
}

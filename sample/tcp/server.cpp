#include <asyncio/net/stream.h>
#include <asyncio/signal.h>
#include <zero/cmdline.h>

asyncio::task::Task<void, std::error_code> handle(asyncio::net::TCPStream stream) {
    const auto address = stream.remoteAddress();
    CO_EXPECT(address);

    fmt::print("connection[{}]\n", *address);

    while (true) {
        std::string message;
        message.resize(1024);

        const auto n = co_await stream.read(std::as_writable_bytes(std::span{message}));
        CO_EXPECT(n);

        if (*n == 0)
            break;

        message.resize(*n);

        fmt::print("receive message: {}\n", message);
        CO_EXPECT(co_await stream.writeAll(std::as_bytes(std::span{message})));
    }

    co_return {};
}

asyncio::task::Task<void, std::error_code> serve(asyncio::net::TCPListener listener) {
    std::expected<void, std::error_code> result;
    asyncio::task::TaskGroup group;

    while (true) {
        auto stream = co_await listener.accept();

        if (!stream) {
            result = std::unexpected{stream.error()};
            break;
        }

        auto task = handle(*std::move(stream));

        group.add(task);
        task.future().fail([](const auto &ec) {
            fmt::print(stderr, "unhandled error: {} ({})\n", ec.message(), ec);
        });
    }

    co_await group;
    co_return result;
}

asyncio::task::Task<void, std::error_code> asyncMain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<std::uint16_t>("port", "remote port");

    cmdline.parse(argc, argv);

    const auto host = cmdline.get<std::string>("host");
    const auto port = cmdline.get<std::uint16_t>("port");

    auto listener = asyncio::net::TCPListener::listen(host, port);
    CO_EXPECT(listener);

    auto signal = asyncio::Signal::make();
    CO_EXPECT(signal);

    co_return co_await race(
        serve(*std::move(listener)),
        signal->on(SIGINT).transform([](const int) {
        })
    );
}

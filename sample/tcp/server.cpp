#include <asyncio/net/stream.h>
#include <asyncio/signal.h>
#include <zero/cmdline.h>
#include <zero/formatter.h>

asyncio::task::Task<void> handle(asyncio::net::TCPStream stream) {
    const auto address = zero::error::guard(stream.remoteAddress());
    fmt::print("Connection: {}\n", address);

    while (true) {
        std::string message;
        message.resize(1024);

        const auto n = zero::error::guard(co_await stream.read(std::as_writable_bytes(std::span{message})));

        if (n == 0)
            break;

        message.resize(n);

        fmt::print("Receive message: {}\n", message);
        zero::error::guard(co_await stream.writeAll(std::as_bytes(std::span{message})));
    }
}

asyncio::task::Task<void> serve(asyncio::net::TCPListener listener) {
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
        task.future().fail([](const auto &e) {
            fmt::print(stderr, "Unhandled exception: {}\n", e);
        });
    }

    co_await group;
    zero::error::guard(std::move(result));
}

asyncio::task::Task<void> asyncMain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<std::string>("ip", "IP address to bind");
    cmdline.add<std::uint16_t>("port", "Port number to listen on");

    cmdline.parse(argc, argv);

    const auto ip = cmdline.get<std::string>("ip");
    const auto port = cmdline.get<std::uint16_t>("port");

    auto listener = zero::error::guard(asyncio::net::TCPListener::listen(ip, port));
    auto signal = asyncio::Signal::make();

    co_await race(
        serve(std::move(listener)),
        asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
            zero::error::guard(co_await signal.on(SIGINT));
        })
    );
}

#include <asyncio/thread.h>
#include <asyncio/signal.h>
#include <asyncio/error.h>
#include <zero/cmdline.h>
#include <httplib.h>

asyncio::task::Task<void> asyncMain(const int argc, char *argv[]) {
    zero::Cmdline cmdline;

    cmdline.add<std::string>("ip", "IP address to bind");
    cmdline.add<std::uint16_t>("port", "Port number to listen on");

    cmdline.parse(argc, argv);

    const auto ip = cmdline.get<std::string>("ip");
    const auto port = cmdline.get<std::uint16_t>("port");

    httplib::Server server;

    server.Get(
        "/hi",
        [](const httplib::Request &, httplib::Response &response) {
            response.set_content("Hello World!", "text/plain");
        }
    );

    auto signal = asyncio::Signal::make();

    co_await race(
        asyncio::toThread(
            [&] {
                if (!server.listen(ip, port)) {
#ifdef _WIN32
                    throw std::system_error{WSAGetLastError(), std::system_category()};
#else
                    throw std::system_error{errno, std::system_category()};
#endif
                }
            },
            [&](std::thread::native_handle_type) -> std::expected<void, std::error_code> {
                server.stop();
                return {};
            }
        ),
        asyncio::task::spawn([&]() -> asyncio::task::Task<void> {
            co_await asyncio::error::guard(signal.on(SIGINT));
        })
    );
}

#include <asyncio/http/request.h>
#include <asyncio/event_loop.h>
#include <zero/log.h>
#include <zero/cmdline.h>
#include <fmt/std.h>

#if __unix__ || __APPLE__
#include <csignal>
#endif

using namespace std::chrono_literals;

int main(int argc, char *argv[]) {
    INIT_CONSOLE_LOG(zero::INFO_LEVEL);

    zero::Cmdline cmdline;

    cmdline.add<asyncio::http::URL>("url", "http request url");

    cmdline.addOptional<std::string>("method", 'm', "http request method", "GET");
    cmdline.addOptional<std::vector<std::string>>("headers", 'h', "http request headers");
    cmdline.addOptional<std::string>("body", '\0', "http request body");
    cmdline.addOptional<std::filesystem::path>("output", '\0', "output file path");

    cmdline.addOptional("json", '\0', "http body with json");
    cmdline.addOptional("form", '\0', "http body with form");

    cmdline.parse(argc, argv);

    const auto url = cmdline.get<asyncio::http::URL>("url");
    const auto method = cmdline.getOptional<std::string>("method");
    const auto headers = cmdline.getOptional<std::vector<std::string>>("headers");
    const auto body = cmdline.getOptional<std::string>("body");
    const auto output = cmdline.getOptional<std::filesystem::path>("output");

    const auto json = cmdline.exist("json");
    const auto form = cmdline.exist("form");

#ifdef _WIN32
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup failed");
        return -1;
    }
#endif

#if __unix__ || __APPLE__
    signal(SIGPIPE, SIG_IGN);
#endif

    asyncio::run([&]() -> zero::async::coroutine::Task<void> {
        asyncio::http::Options options;

        if (headers) {
            for (const auto &header: *headers) {
                const auto tokens = zero::strings::split(header, "=");

                if (tokens.size() != 2) {
                    LOG_WARNING("invalid header[{}]", header);
                    continue;
                }

                options.headers[tokens[0]] = tokens[1];
            }
        }

        const auto result = makeRequests(options);

        if (!result) {
            LOG_ERROR("make result failed[{}]", result.error());
            co_return;
        }

        const auto response = co_await [&, requests = *result] {
            if (!body)
                return requests->request(*method, url, options);

            if (json)
                requests->request(*method, url, options, nlohmann::json::parse(*body));

            if (form) {
                std::map<std::string, std::variant<std::string, std::filesystem::path>> data;

                for (const auto &part: zero::strings::split(*body, ",")) {
                    const auto tokens = zero::strings::split(part, "=");

                    if (tokens.size() != 2)
                        continue;

                    if (tokens[1].starts_with("@"))
                        data[tokens[0]] = std::filesystem::path(tokens[1].substr(1));
                    else
                        data[tokens[0]] = tokens[1];
                }

                requests->request(*method, url, options, data);
            }

            return requests->request(*method, url, options, *body);
        }();

        if (!response) {
            LOG_ERROR("request failed[{}]", response.error());
            co_return;
        }

        if (output) {
            if (const auto res = co_await response->output(*output); !res) {
                LOG_ERROR("output to {} failed[{}]", *output, res.error());
                co_return;
            }

            co_return;
        }

        const auto content = co_await response->string();

        if (!content) {
            LOG_ERROR("get response content failed[{}]", content.error());
            co_return;
        }

        fmt::print("{}", *content);
    });

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}

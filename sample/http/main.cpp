#include <asyncio/http/request.h>
#include <asyncio/event_loop.h>
#include <zero/log.h>
#include <zero/cmdline.h>
#include <fmt/std.h>

#ifdef __unix__
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

    auto url = cmdline.get<asyncio::http::URL>("url");
    auto method = cmdline.getOptional<std::string>("method");
    auto headers = cmdline.getOptional<std::vector<std::string>>("headers");
    auto body = cmdline.getOptional<std::string>("body");
    auto output = cmdline.getOptional<std::filesystem::path>("output");

    auto json = cmdline.exist("json");
    auto form = cmdline.exist("form");

#ifdef _WIN32
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup failed");
        return -1;
    }
#endif

#ifdef __unix__
    signal(SIGPIPE, SIG_IGN);
#endif

    asyncio::run([&]() -> zero::async::coroutine::Task<void> {
        asyncio::http::Options options;

        if (headers) {
            for (const auto &header: *headers) {
                auto tokens = zero::strings::split(header, "=");

                if (tokens.size() != 2) {
                    LOG_WARNING("invalid header[{}]", header);
                    continue;
                }

                options.headers[tokens[0]] = tokens[1];
            }
        }

        auto result = asyncio::http::makeRequests(options);

        if (!result) {
            LOG_ERROR("make result failed[{}]", result.error());
            co_return;
        }

        auto response = std::move(co_await [&, requests = *result]() {
            if (!body)
                return requests->request(*method, url, options);

            if (json)
                requests->request(*method, url, options, nlohmann::json::parse(*body));

            if (form) {
                std::map<std::string, std::variant<std::string, std::filesystem::path>> data;

                for (const auto &part: zero::strings::split(*body, ",")) {
                    auto tokens = zero::strings::split(part, "=");

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
        }());

        if (!response) {
            LOG_ERROR("request failed[{}]", response.error());
            co_return;
        }

        if (output) {
            auto res = co_await response->output(*output);

            if (!res) {
                LOG_ERROR("output to {} failed[{}]", *output, res.error());
                co_return;
            }

            co_return;
        }

        auto content = co_await response->string();

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
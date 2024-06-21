#include <asyncio/http/request.h>
#include <zero/cmdline.h>

asyncio::task::Task<void, std::error_code> amain(const int argc, char *argv[]) {
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

    asyncio::http::Options options;

    if (headers) {
        for (const auto &header: *headers) {
            const auto tokens = zero::strings::split(header, "=");

            if (tokens.size() != 2) {
                fmt::print(stderr, "invalid header[{}]\n", header);
                continue;
            }

            options.headers[tokens[0]] = tokens[1];
        }
    }

    auto requests = asyncio::http::Requests::make(options);
    CO_EXPECT(requests);

    const auto response = co_await [&] {
        if (!body)
            return requests->request(*method, url, options);

        if (json)
            return requests->request(*method, url, options, nlohmann::json::parse(*body));

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

            return requests->request(*method, url, options, data);
        }

        return requests->request(*method, url, options, *body);
    }();
    CO_EXPECT(response);

    if (output) {
        CO_EXPECT(co_await response->output(*output));
    }

    const auto content = co_await response->string();
    CO_EXPECT(content);

    fmt::print("{}", *content);
    co_return {};
}

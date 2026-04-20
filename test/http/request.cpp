#include <catch_extensions.h>
#include <asyncio/http/request.h>
#include <asyncio/net/stream.h>
#include <asyncio/fs.h>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <regex>

namespace {
    struct People {
        std::string name;
        int age{};

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(People, name, age);
    };

    asyncio::task::Task<std::string> serve(asyncio::net::TCPListener listener) {
        using namespace std::string_view_literals;

        auto stream = co_await asyncio::error::guard(listener.accept());

        std::string rawRequest;

        while (true) {
            std::array<std::byte, 1024> data{};
            const auto n = co_await asyncio::error::guard(stream.read(data));

            rawRequest.append(reinterpret_cast<const char *>(data.data()), n);

            if (rawRequest.contains("\r\n\r\n"))
                break;
        }

        if (std::smatch match; std::regex_search(rawRequest, match, std::regex(R"(Content-Length: (\d+))"))) {
            const auto length = co_await asyncio::error::guard(zero::strings::toNumber<std::size_t>(match.str(1)));

            std::vector<std::byte> remaining(length - (rawRequest.size() - rawRequest.find("\r\n\r\n") - 4));
            co_await asyncio::error::guard(stream.readExactly(remaining));

            rawRequest.append(reinterpret_cast<const char *>(remaining.data()), remaining.size());
        }

        co_await asyncio::error::guard(stream.writeAll(
            std::as_bytes(
                std::span{
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Length: 11\r\n"
                    "Content-Type: text/html\r\n"
                    "Server: asyncio\r\n"
                    "Set-Cookie: user=jack\r\n\r\n"
                    "hello world"sv
                }
            )
        ));

        co_return rawRequest;
    }
}

ASYNC_TEST_CASE("requests", "[http::request]") {
    const auto temp = co_await asyncio::error::guard(asyncio::fs::temporaryDirectory());
    const auto path = temp / GENERATE(take(1, randomAlphanumericString(8, 64)));

    auto listener = co_await asyncio::error::guard(asyncio::net::TCPListener::listen("127.0.0.1", 0));
    const auto address = co_await asyncio::error::guard(listener.address());

    auto url = co_await asyncio::error::guard(asyncio::http::URL::from("http://127.0.0.1"));
    url.port(std::get<asyncio::net::IPv4Address>(address).port);

    auto task = serve(std::move(listener));

    SECTION("response") {
        auto requests = asyncio::http::Requests::make();
        auto response = co_await asyncio::error::guard(requests.get(url));

        SECTION("status code") {
            REQUIRE(response.statusCode() == 200);
        }

        SECTION("content length") {
            REQUIRE(response.contentLength() == 11);
        }

        SECTION("cookies") {
            const auto cookies = response.cookies();
            REQUIRE_THAT(cookies, Catch::Matchers::SizeIs(1));
            REQUIRE_THAT(cookies.front(), Catch::Matchers::ContainsSubstring("jack"));
        }

        SECTION("header") {
            REQUIRE(response.header("Server") == "asyncio");
        }

        SECTION("string") {
            REQUIRE(co_await response.string() == "hello world");
        }

        SECTION("output") {
            REQUIRE(co_await response.output(path));
            REQUIRE(co_await asyncio::error::guard(asyncio::fs::readString(path)) == "hello world");
            co_await asyncio::error::guard(asyncio::fs::remove(path));
        }

        SECTION("read") {
            std::string content;
            content.resize(11);

            REQUIRE(co_await response.readExactly(std::as_writable_bytes(std::span{content})));
            REQUIRE(content == "hello world");
        }

        // Avoid response not being written completely
        co_await asyncio::error::guard(response.readAll());
        co_await task;
    }

    SECTION("options") {
        auto requests = asyncio::http::Requests::make();

        asyncio::http::Options options;

        SECTION("headers") {
            options.headers["Custom-Header"] = "Custom-Value";

            auto response = co_await requests.get(url, options);
            REQUIRE(response);
            co_await asyncio::error::guard(response->readAll());

            const auto rawRequest = co_await task;
            REQUIRE_THAT(rawRequest, Catch::Matchers::ContainsSubstring("Custom-Header: Custom-Value"));
        }

        SECTION("cookies") {
            options.cookies["Custom-Cookie"] = "Custom-Value";

            auto response = co_await requests.get(url, options);
            REQUIRE(response);
            co_await asyncio::error::guard(response->readAll());

            const auto rawRequest = co_await task;
            REQUIRE_THAT(rawRequest, Catch::Matchers::ContainsSubstring("Cookie: Custom-Cookie=Custom-Value"));
        }

        SECTION("user agent") {
            options.userAgent = "Custom Agent";

            auto response = co_await requests.get(url, options);
            REQUIRE(response);
            co_await asyncio::error::guard(response->readAll());

            const auto rawRequest = co_await task;
            REQUIRE_THAT(rawRequest, Catch::Matchers::ContainsSubstring("User-Agent: Custom Agent"));
        }

        SECTION("hook") {
            options.hooks.emplace_back(
                [](const asyncio::http::Connection &connection) -> std::expected<void, std::error_code> {
                    Z_EXPECT(asyncio::http::expected([&] {
                        return curl_easy_setopt(connection.easy.get(), CURLOPT_USERAGENT, "Custom Agent");
                    }));
                    return {};
                }
            );

            auto response = co_await requests.get(url, options);
            REQUIRE(response);
            co_await asyncio::error::guard(response->readAll());

            const auto rawRequest = co_await task;
            REQUIRE_THAT(rawRequest, Catch::Matchers::ContainsSubstring("User-Agent: Custom Agent"));
        }
    }

    SECTION("session options") {
        auto requests = asyncio::http::Requests::make({.userAgent = "Custom Agent"});

        SECTION("default") {
            auto response = co_await requests.get(url);
            REQUIRE(response);
            co_await asyncio::error::guard(response->readAll());

            const auto rawRequest = co_await task;
            REQUIRE_THAT(rawRequest, Catch::Matchers::ContainsSubstring("User-Agent: Custom Agent"));
        }

        SECTION("override") {
            auto response = co_await requests.get(url, asyncio::http::Options{.userAgent = "Override"});
            REQUIRE(response);
            co_await asyncio::error::guard(response->readAll());

            const auto rawRequest = co_await task;
            REQUIRE_THAT(rawRequest, Catch::Matchers::ContainsSubstring("User-Agent: Override"));
        }
    }

    SECTION("request") {
        auto requests = asyncio::http::Requests::make();

        SECTION("methods") {
            SECTION("get") {
                auto response = co_await requests.get(url);
                REQUIRE(response);
                co_await asyncio::error::guard(response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE_THAT(rawRequest, Catch::Matchers::StartsWith("GET / HTTP/1.1\r\n"));
            }

            SECTION("post") {
                auto response = co_await requests.post(url, "");
                REQUIRE(response);
                co_await asyncio::error::guard(response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE_THAT(rawRequest, Catch::Matchers::StartsWith("POST / HTTP/1.1\r\n"));
            }

            SECTION("put") {
                auto response = co_await requests.put(url, "");
                REQUIRE(response);
                co_await asyncio::error::guard(response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE_THAT(rawRequest, Catch::Matchers::StartsWith("PUT / HTTP/1.1\r\n"));
            }

            SECTION("delete") {
                auto response = co_await requests.del(url);
                REQUIRE(response);
                co_await asyncio::error::guard(response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE_THAT(rawRequest, Catch::Matchers::StartsWith("DELETE / HTTP/1.1\r\n"));
            }
        }

        SECTION("payload") {
            SECTION("string") {
                auto response = co_await requests.post(url, "hello world");
                REQUIRE(response);
                co_await asyncio::error::guard(response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE_THAT(rawRequest, Catch::Matchers::EndsWith("hello world"));
            }

            SECTION("form") {
                auto response = co_await requests.post(url, std::map<std::string, std::string>{{"name", "jack"}});
                REQUIRE(response);
                co_await asyncio::error::guard(response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE_THAT(rawRequest, Catch::Matchers::EndsWith("name=jack"));
            }

            SECTION("multipart") {
                co_await asyncio::error::guard(asyncio::fs::write(path, "hello world"));

                auto response = co_await requests.post(
                    url,
                    std::map<std::string, std::variant<std::string, std::filesystem::path>>{
                        {"name", std::string{"jack"}},
                        {"file", path}
                    }
                );
                REQUIRE(response);
                co_await asyncio::error::guard(response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE_THAT(rawRequest, Catch::Matchers::ContainsSubstring("Content-Type: multipart/form-data"));
                REQUIRE_THAT(rawRequest, Catch::Matchers::ContainsSubstring("jack"));
                REQUIRE_THAT(rawRequest, Catch::Matchers::ContainsSubstring("hello world"));

                co_await asyncio::error::guard(asyncio::fs::remove(path));
            }

            SECTION("json") {
                SECTION("json object") {
                    auto response = co_await requests.post(url, nlohmann::json{{"name", "jack"}, {"age", 18}});
                    REQUIRE(response);
                    co_await asyncio::error::guard(response->readAll());
                }

                SECTION("serializable object") {
                    auto response = co_await requests.post(url, People{"jack", 18});
                    REQUIRE(response);
                    co_await asyncio::error::guard(response->readAll());
                }

                const auto rawRequest = co_await task;
                REQUIRE_THAT(rawRequest, Catch::Matchers::ContainsSubstring(R"("name":"jack")"));
                REQUIRE_THAT(rawRequest, Catch::Matchers::ContainsSubstring(R"("age":18)"));
            }

            SECTION("stream") {
                const auto method = GENERATE("POST", "PUT");
                const auto input = GENERATE(take(1, randomString(1, 102400)));

                co_await asyncio::error::guard(asyncio::fs::write(path, input));
                auto file = co_await asyncio::error::guard(asyncio::fs::open(path, O_RDONLY));

                auto response = co_await requests.request(method, url, std::nullopt, std::move(file));
                REQUIRE(response);
                co_await asyncio::error::guard(response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE_THAT(rawRequest, Catch::Matchers::EndsWith(input));

                co_await asyncio::error::guard(asyncio::fs::remove(path));
            }
        }
    }
}

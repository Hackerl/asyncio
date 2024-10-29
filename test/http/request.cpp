#include <catch_extensions.h>
#include <asyncio/http/request.h>
#include <asyncio/net/stream.h>
#include <asyncio/fs.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <regex>

struct People {
    std::string name;
    int age{};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(People, name, age);
};

asyncio::task::Task<std::string, std::error_code> serve(asyncio::net::TCPListener listener) {
    using namespace std::string_view_literals;

    auto stream = co_await listener.accept();
    CO_EXPECT(stream);

    std::string rawRequest;

    while (true) {
        std::array<std::byte, 1024> data{};
        const auto n = co_await stream->read(data);
        CO_EXPECT(n);

        rawRequest.append(reinterpret_cast<const char *>(data.data()), *n);

        if (rawRequest.contains("\r\n\r\n"))
            break;
    }

    std::smatch match;

    if (std::regex_search(rawRequest, match, std::regex(R"(Content-Length: (\d+))"))) {
        const auto length = zero::strings::toNumber<std::size_t>(match.str(1));
        CO_EXPECT(length);

        std::vector<std::byte> remain(*length - (rawRequest.size() - rawRequest.find("\r\n\r\n") - 4));
        CO_EXPECT(co_await stream->readExactly(remain));

        rawRequest.append(reinterpret_cast<const char *>(remain.data()), remain.size());
    }

    CO_EXPECT(co_await stream->writeAll(
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

ASYNC_TEST_CASE("requests", "[http]") {
    const auto temp = co_await asyncio::fs::temporaryDirectory();
    REQUIRE(temp);

    const auto path = *temp / "asyncio-http-requests";

    auto listener = asyncio::net::TCPListener::listen("127.0.0.1", 0);
    REQUIRE(listener);

    const auto address = listener->address();
    REQUIRE(address);

    auto url = asyncio::http::URL::from("http://127.0.0.1");
    REQUIRE(url);

    url->port(std::get<asyncio::net::IPv4Address>(*address).port);

    auto task = serve(*std::move(listener));

    SECTION("response") {
        auto requests = asyncio::http::Requests::make();
        REQUIRE(requests);

        auto response = co_await requests->get(*url);
        REQUIRE(response);

        SECTION("status code") {
            REQUIRE(response->statusCode() == 200);
        }

        SECTION("content length") {
            REQUIRE(response->contentLength() == 11);
        }

        SECTION("cookies") {
            const auto cookies = response->cookies();
            REQUIRE_THAT(cookies, Catch::Matchers::SizeIs(1));
            REQUIRE_THAT(cookies.front(), Catch::Matchers::ContainsSubstring("jack"));
        }

        SECTION("header") {
            REQUIRE(response->header("Server") == "asyncio");
        }

        SECTION("string") {
            REQUIRE(co_await response->string() == "hello world");
        }

        SECTION("output") {
            REQUIRE(co_await response->output(path));
            REQUIRE(co_await asyncio::fs::readString(path) == "hello world");
            REQUIRE(co_await asyncio::fs::remove(path));
        }

        SECTION("read") {
            std::string content;
            content.resize(11);

            REQUIRE(co_await response->readExactly(std::as_writable_bytes(std::span{content})));
            REQUIRE(content == "hello world");
        }

        // avoid response not being written completely
        REQUIRE(co_await response->readAll());
        REQUIRE(co_await task);
    }

    SECTION("options") {
        auto requests = asyncio::http::Requests::make();
        REQUIRE(requests);

        asyncio::http::Options options;

        SECTION("headers") {
            options.headers["Custom-Header"] = "Custom-Value";

            auto response = co_await requests->get(*url, options);
            REQUIRE(response);
            REQUIRE(co_await response->readAll());

            const auto rawRequest = co_await task;
            REQUIRE(rawRequest);
            REQUIRE_THAT(*rawRequest, Catch::Matchers::ContainsSubstring("Custom-Header: Custom-Value"));
        }

        SECTION("cookies") {
            options.cookies["Custom-Cookie"] = "Custom-Value";

            auto response = co_await requests->get(*url, options);
            REQUIRE(response);
            REQUIRE(co_await response->readAll());

            const auto rawRequest = co_await task;
            REQUIRE(rawRequest);
            REQUIRE_THAT(*rawRequest, Catch::Matchers::ContainsSubstring("Cookie: Custom-Cookie=Custom-Value"));
        }

        SECTION("user agent") {
            options.userAgent = "Custom Agent";

            auto response = co_await requests->get(*url, options);
            REQUIRE(response);
            REQUIRE(co_await response->readAll());

            const auto rawRequest = co_await task;
            REQUIRE(rawRequest);
            REQUIRE_THAT(*rawRequest, Catch::Matchers::ContainsSubstring("User-Agent: Custom Agent"));
        }

        SECTION("hook") {
            options.hooks.emplace_back(
                [](const asyncio::http::Connection &connection) -> std::expected<void, std::error_code> {
                    curl_easy_setopt(connection.easy.get(), CURLOPT_USERAGENT, "Custom Agent");
                    return {};
                }
            );

            auto response = co_await requests->get(*url, options);
            REQUIRE(response);
            REQUIRE(co_await response->readAll());

            const auto rawRequest = co_await task;
            REQUIRE(rawRequest);
            REQUIRE_THAT(*rawRequest, Catch::Matchers::ContainsSubstring("User-Agent: Custom Agent"));
        }
    }

    SECTION("session options") {
        auto requests = asyncio::http::Requests::make({
            .userAgent = "Custom Agent"
        });
        REQUIRE(requests);

        SECTION("default") {
            auto response = co_await requests->get(*url);
            REQUIRE(response);
            REQUIRE(co_await response->readAll());

            const auto rawRequest = co_await task;
            REQUIRE(rawRequest);
            REQUIRE_THAT(*rawRequest, Catch::Matchers::ContainsSubstring("User-Agent: Custom Agent"));
        }

        SECTION("override") {
            auto response = co_await requests->get(*url, asyncio::http::Options{.userAgent = "Override"});
            REQUIRE(response);
            REQUIRE(co_await response->readAll());

            const auto rawRequest = co_await task;
            REQUIRE(rawRequest);
            REQUIRE_THAT(*rawRequest, Catch::Matchers::ContainsSubstring("User-Agent: Override"));
        }
    }

    SECTION("request") {
        auto requests = asyncio::http::Requests::make();
        REQUIRE(requests);

        SECTION("methods") {
            SECTION("get") {
                auto response = co_await requests->get(*url);
                REQUIRE(response);
                REQUIRE(co_await response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE(rawRequest);
                REQUIRE_THAT(*rawRequest, Catch::Matchers::StartsWith("GET / HTTP/1.1\r\n"));
            }

            SECTION("post") {
                auto response = co_await requests->post(*url, "");
                REQUIRE(response);
                REQUIRE(co_await response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE(rawRequest);
                REQUIRE_THAT(*rawRequest, Catch::Matchers::StartsWith("POST / HTTP/1.1\r\n"));
            }

            SECTION("put") {
                auto response = co_await requests->put(*url, "");
                REQUIRE(response);
                REQUIRE(co_await response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE(rawRequest);
                REQUIRE_THAT(*rawRequest, Catch::Matchers::StartsWith("PUT / HTTP/1.1\r\n"));
            }

            SECTION("delete") {
                auto response = co_await requests->del(*url);
                REQUIRE(response);
                REQUIRE(co_await response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE(rawRequest);
                REQUIRE_THAT(*rawRequest, Catch::Matchers::StartsWith("DELETE / HTTP/1.1\r\n"));
            }
        }

        SECTION("payload") {
            SECTION("string") {
                auto response = co_await requests->post(*url, "hello world");
                REQUIRE(response);
                REQUIRE(co_await response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE(rawRequest);
                REQUIRE_THAT(*rawRequest, Catch::Matchers::EndsWith("hello world"));
            }

            SECTION("form") {
                auto response = co_await requests->post(*url, std::map<std::string, std::string>{{"name", "jack"}});
                REQUIRE(response);
                REQUIRE(co_await response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE(rawRequest);
                REQUIRE_THAT(*rawRequest, Catch::Matchers::EndsWith("name=jack"));
            }

            SECTION("multipart") {
                REQUIRE(co_await asyncio::fs::write(path, "hello world"));

                auto response = co_await requests->post(
                    *url,
                    std::map<std::string, std::variant<std::string, std::filesystem::path>>{
                        {"name", std::string{"jack"}},
                        {"file", path}
                    }
                );
                REQUIRE(response);
                REQUIRE(co_await response->readAll());

                const auto rawRequest = co_await task;
                REQUIRE(rawRequest);
                REQUIRE_THAT(*rawRequest, Catch::Matchers::ContainsSubstring("Content-Type: multipart/form-data"));
                REQUIRE_THAT(*rawRequest, Catch::Matchers::ContainsSubstring("jack"));
                REQUIRE_THAT(*rawRequest, Catch::Matchers::ContainsSubstring("hello world"));

                REQUIRE(co_await asyncio::fs::remove(path));
            }

            SECTION("json") {
                SECTION("json object") {
                    auto response = co_await requests->post(*url, nlohmann::json{{"name", "jack"}, {"age", 18}});
                    REQUIRE(response);
                    REQUIRE(co_await response->readAll());
                }

                SECTION("serializable object") {
                    auto response = co_await requests->post(*url, People{"jack", 18});
                    REQUIRE(response);
                    REQUIRE(co_await response->readAll());
                }

                const auto rawRequest = co_await task;
                REQUIRE(rawRequest);
                REQUIRE_THAT(*rawRequest, Catch::Matchers::ContainsSubstring(R"("name":"jack")"));
                REQUIRE_THAT(*rawRequest, Catch::Matchers::ContainsSubstring(R"("age":18)"));
            }
        }
    }
}

#include <asyncio/http/request.h>
#include <asyncio/event_loop.h>
#include <asyncio/net/stream.h>
#include <catch2/catch_test_macros.hpp>
#include <fstream>

constexpr auto URL = "http://localhost:30000/object?id=0";

struct People {
    std::string name;
    int age{};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(People, name, age);

TEST_CASE("http requests", "[http]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        auto listener = asyncio::net::stream::listen("127.0.0.1", 30000);
        REQUIRE(listener);

        SECTION("get") {
            SECTION("normal") {
                co_await allSettled(
                    [](auto l) -> zero::async::coroutine::Task<void> {
                        auto buffer = co_await l.accept();
                        REQUIRE(buffer);

                        auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "GET /object?id=0 HTTP/1.1");

                        while (true) {
                            line = co_await buffer->readLine();
                            REQUIRE(line);

                            if (line->empty())
                                break;
                        }

                        constexpr std::string_view response = "HTTP/1.1 200 OK\r\n"
                            "Content-Length: 11\r\n"
                            "Content-Type: text/html\r\n"
                            "Server: asyncio\r\n"
                            "Set-Cookie: user=jack\r\n\r\n"
                            "hello world";

                        auto result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);
                    }(std::move(*listener)),
                    []() -> zero::async::coroutine::Task<void> {
                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        const auto requests = asyncio::http::makeRequests();
                        REQUIRE(requests);

                        const auto response = co_await requests.value()->get(*url);
                        REQUIRE(response);
                        REQUIRE(response->statusCode() == 200);

                        const auto length = response->contentLength();
                        REQUIRE(length);
                        REQUIRE(*length == 11);

                        const auto type = response->contentType();
                        REQUIRE(type);
                        REQUIRE(*type == "text/html");

                        const auto server = response->header("Server");
                        REQUIRE(server);
                        REQUIRE(*server == "asyncio");

                        const auto cookies = response->cookies();
                        REQUIRE(cookies.size() == 1);
                        REQUIRE(cookies[0].find("jack") != std::string::npos);

                        const auto content = co_await response->string();
                        REQUIRE(content);
                        REQUIRE(*content == "hello world");
                    }()
                );
            }

            SECTION("empty body") {
                co_await allSettled(
                    [](auto l) -> zero::async::coroutine::Task<void> {
                        auto buffer = co_await l.accept();
                        REQUIRE(buffer);

                        auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "GET /object?id=0 HTTP/1.1");

                        while (true) {
                            line = co_await buffer->readLine();
                            REQUIRE(line);

                            if (line->empty())
                                break;
                        }

                        constexpr std::string_view response = "HTTP/1.1 200 OK\r\n"
                            "Content-Length: 0\r\n\r\n";

                        auto result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);
                    }(std::move(*listener)),
                    []() -> zero::async::coroutine::Task<void> {
                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        const auto requests = asyncio::http::makeRequests();
                        REQUIRE(requests);

                        const auto response = co_await requests.value()->get(*url);
                        REQUIRE(response);

                        const auto content = co_await response->string();
                        REQUIRE(content);
                        REQUIRE(content->empty());
                    }()
                );
            }

            SECTION("get json") {
                co_await allSettled(
                    [](auto l) -> zero::async::coroutine::Task<void> {
                        auto buffer = co_await l.accept();
                        REQUIRE(buffer);

                        auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "GET /object?id=0 HTTP/1.1");

                        while (true) {
                            line = co_await buffer->readLine();
                            REQUIRE(line);

                            if (line->empty())
                                break;
                        }

                        constexpr std::string_view response = "HTTP/1.1 200 OK\r\n"
                            "Content-Length: 27\r\n\r\n"
                            "{\"name\": \"rose\", \"age\": 17}";

                        auto result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);
                    }(std::move(*listener)),
                    []() -> zero::async::coroutine::Task<void> {
                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        const auto requests = asyncio::http::makeRequests();
                        REQUIRE(requests);

                        const auto response = co_await requests.value()->get(*url);
                        REQUIRE(response);

                        const auto people = co_await response->json<People>();
                        REQUIRE(people);
                        REQUIRE(people->name == "rose");
                        REQUIRE(people->age == 17);
                    }()
                );
            }
        }

        SECTION("post") {
            SECTION("post form") {
                co_await allSettled(
                    [](auto l) -> zero::async::coroutine::Task<void> {
                        auto buffer = co_await l.accept();
                        REQUIRE(buffer);

                        auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "POST /object?id=0 HTTP/1.1");

                        std::map<std::string, std::string> headers;

                        while (true) {
                            line = co_await buffer->readLine();
                            REQUIRE(line);

                            if (line->empty())
                                break;

                            const auto tokens = zero::strings::split(*line, ":", 1);
                            REQUIRE(tokens.size() == 2);
                            headers[tokens[0]] = zero::strings::trim(tokens[1]);
                        }

                        const auto it = headers.find("Content-Length");
                        REQUIRE(it != headers.end());

                        const auto length = zero::strings::toNumber<size_t>(it->second);
                        REQUIRE(length);

                        std::vector<std::byte> data(*length);
                        auto result = co_await buffer->readExactly(data);
                        REQUIRE(result);
                        REQUIRE(memcmp(data.data(), "name=jack", 9) == 0);

                        constexpr std::string_view response = "HTTP/1.1 200 OK\r\n"
                            "Content-Length: 11\r\n\r\n"
                            "hello world";

                        result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);
                    }(std::move(*listener)),
                    []() -> zero::async::coroutine::Task<void> {
                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        const auto requests = asyncio::http::makeRequests();
                        REQUIRE(requests);

                        const std::map<std::string, std::string> payload = {{"name", "jack"}};
                        const auto response = co_await requests.value()->post(*url, payload);
                        REQUIRE(response);

                        const auto content = co_await response->string();
                        REQUIRE(content);
                        REQUIRE(*content == "hello world");
                    }()
                );
            }

            SECTION("post file") {
                co_await allSettled(
                    [](auto l) -> zero::async::coroutine::Task<void> {
                        auto buffer = co_await l.accept();
                        REQUIRE(buffer);

                        auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "POST /object?id=0 HTTP/1.1");

                        std::map<std::string, std::string> headers;

                        while (true) {
                            line = co_await buffer->readLine();
                            REQUIRE(line);

                            if (line->empty())
                                break;

                            auto tokens = zero::strings::split(*line, ":", 1);
                            REQUIRE(tokens.size() == 2);
                            headers[tokens[0]] = zero::strings::trim(tokens[1]);
                        }

                        const auto it = headers.find("Content-Length");
                        REQUIRE(it != headers.end());

                        const auto length = zero::strings::toNumber<size_t>(it->second);
                        REQUIRE(length);

                        std::vector<std::byte> data(*length);
                        auto result = co_await buffer->readExactly(data);
                        REQUIRE(result);

                        constexpr std::string_view response = "HTTP/1.1 200 OK\r\n"
                            "Content-Length: 11\r\n\r\n"
                            "hello world";

                        result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);
                    }(std::move(*listener)),
                    []() -> zero::async::coroutine::Task<void> {
                        const auto path = std::filesystem::temp_directory_path() / "asyncio-requests";
                        std::ofstream stream(path);
                        REQUIRE(stream.is_open());

                        stream << "hello world";
                        stream.close();

                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        const auto requests = asyncio::http::makeRequests();
                        REQUIRE(requests);

                        const std::map<std::string, std::filesystem::path> payload = {{"file", path}};
                        const auto response = co_await requests.value()->post(*url, payload);
                        REQUIRE(response);

                        const auto content = co_await response->string();
                        REQUIRE(content);
                        REQUIRE(*content == "hello world");

                        REQUIRE(std::filesystem::remove(path));
                    }()
                );
            }

            SECTION("post multipart") {
                co_await allSettled(
                    [](auto l) -> zero::async::coroutine::Task<void> {
                        auto buffer = co_await l.accept();
                        REQUIRE(buffer);

                        auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "POST /object?id=0 HTTP/1.1");

                        std::map<std::string, std::string> headers;

                        while (true) {
                            line = co_await buffer->readLine();
                            REQUIRE(line);

                            if (line->empty())
                                break;

                            auto tokens = zero::strings::split(*line, ":", 1);
                            REQUIRE(tokens.size() == 2);
                            headers[tokens[0]] = zero::strings::trim(tokens[1]);
                        }

                        const auto it = headers.find("Content-Length");
                        REQUIRE(it != headers.end());

                        const auto length = zero::strings::toNumber<size_t>(it->second);
                        REQUIRE(length);

                        std::vector<std::byte> data(*length);
                        auto result = co_await buffer->readExactly(data);
                        REQUIRE(result);

                        constexpr std::string_view response = "HTTP/1.1 200 OK\r\n"
                            "Content-Length: 11\r\n\r\n"
                            "hello world";

                        result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);
                    }(std::move(*listener)),
                    []() -> zero::async::coroutine::Task<void> {
                        const auto path = std::filesystem::temp_directory_path() / "asyncio-requests";
                        std::ofstream stream(path);
                        REQUIRE(stream.is_open());

                        stream << "hello world";
                        stream.close();

                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        const auto requests = asyncio::http::makeRequests();
                        REQUIRE(requests);

                        const std::map<std::string, std::variant<std::string, std::filesystem::path>> payload = {
                            {"name", std::string{"jack"}},
                            {"file", path}
                        };

                        const auto response = co_await requests.value()->post(*url, payload);
                        REQUIRE(response);

                        const auto content = co_await response->string();
                        REQUIRE(content);
                        REQUIRE(*content == "hello world");

                        REQUIRE(std::filesystem::remove(path));
                    }()
                );
            }

            SECTION("post json") {
                co_await allSettled(
                    [](auto l) -> zero::async::coroutine::Task<void> {
                        auto buffer = co_await l.accept();
                        REQUIRE(buffer);

                        auto line = co_await buffer->readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "POST /object?id=0 HTTP/1.1");

                        std::map<std::string, std::string> headers;

                        while (true) {
                            line = co_await buffer->readLine();
                            REQUIRE(line);

                            if (line->empty())
                                break;

                            auto tokens = zero::strings::split(*line, ":", 1);
                            REQUIRE(tokens.size() == 2);
                            headers[tokens[0]] = zero::strings::trim(tokens[1]);
                        }

                        auto it = headers.find("Content-Type");
                        REQUIRE(it != headers.end());
                        REQUIRE(it->second == "application/json");

                        it = headers.find("Content-Length");
                        REQUIRE(it != headers.end());

                        const auto length = zero::strings::toNumber<size_t>(it->second);
                        REQUIRE(length);

                        std::vector<std::byte> data(*length);
                        auto result = co_await buffer->readExactly(data);
                        REQUIRE(result);

                        const auto [name, age] = nlohmann::json::parse(data).get<People>();
                        REQUIRE(name == "jack");
                        REQUIRE(age == 18);

                        constexpr std::string_view response = "HTTP/1.1 200 OK\r\n"
                            "Content-Length: 11\r\n\r\n"
                            "hello world";

                        result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);
                    }(std::move(*listener)),
                    []() -> zero::async::coroutine::Task<void> {
                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        const auto requests = asyncio::http::makeRequests();
                        REQUIRE(requests);

                        const auto response = co_await requests.value()->post(*url, People{"jack", 18});
                        REQUIRE(response);

                        const auto content = co_await response->string();
                        REQUIRE(content);
                        REQUIRE(*content == "hello world");
                    }()
                );
            }
        }

        SECTION("output to file") {
            co_await allSettled(
                [](auto l) -> zero::async::coroutine::Task<void> {
                    auto buffer = co_await l.accept();
                    REQUIRE(buffer);

                    auto line = co_await buffer->readLine();
                    REQUIRE(line);
                    REQUIRE(*line == "GET /object?id=0 HTTP/1.1");

                    while (true) {
                        line = co_await buffer->readLine();
                        REQUIRE(line);

                        if (line->empty())
                            break;
                    }

                    constexpr std::string_view response = "HTTP/1.1 200 OK\r\n"
                        "Content-Length: 11\r\n\r\n"
                        "hello world";

                    auto result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                    REQUIRE(result);

                    result = co_await buffer->flush();
                    REQUIRE(result);
                }(std::move(*listener)),
                []() -> zero::async::coroutine::Task<void> {
                    const auto url = asyncio::http::URL::from(URL);
                    REQUIRE(url);

                    const auto requests = asyncio::http::makeRequests();
                    REQUIRE(requests);

                    const auto response = co_await requests.value()->get(*url);
                    REQUIRE(response);

                    const auto path = std::filesystem::temp_directory_path() / "asyncio-requests";
                    const auto result = co_await response->output(path);
                    REQUIRE(result);

                    std::ifstream stream(path);
                    REQUIRE(stream.is_open());
                    REQUIRE(
                        std::string{std::istreambuf_iterator(stream), std::istreambuf_iterator<char>()}
                        == "hello world"
                    );

                    stream.close();
                    REQUIRE(std::filesystem::remove(path));
                }()
            );
        }
    });
}

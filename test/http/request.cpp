#include <asyncio/http/request.h>
#include <asyncio/net/stream.h>
#include <asyncio/buffer.h>
#include <zero/filesystem/fs.h>
#include <catch2/catch_test_macros.hpp>

constexpr auto URL = "http://localhost:30000/object?id=0";

struct People {
    std::string name;
    int age{};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(People, name, age);

TEST_CASE("http requests", "[http]") {
    const auto result = asyncio::run([]() -> asyncio::task::Task<void> {
        auto listener = asyncio::net::TCPListener::listen("127.0.0.1", 30000);
        REQUIRE(listener);

        SECTION("get") {
            SECTION("normal") {
                co_await allSettled(
                    [](auto l) -> asyncio::task::Task<void> {
                        using namespace std::string_view_literals;

                        auto stream = co_await l.accept().transform([](asyncio::net::TCPStream &&value) {
                            return std::make_shared<asyncio::net::TCPStream>(std::move(value));
                        });
                        REQUIRE(stream);

                        asyncio::BufReader reader(*stream);

                        auto line = co_await reader.readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "GET /object?id=0 HTTP/1.1");

                        while (true) {
                            line = co_await reader.readLine();
                            REQUIRE(line);

                            if (line->empty())
                                break;
                        }

                        const auto res = co_await stream.value()->writeAll(
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
                        );
                        REQUIRE(res);
                    }(*std::move(listener)),
                    []() -> asyncio::task::Task<void> {
                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        auto requests = asyncio::http::Requests::make();
                        REQUIRE(requests);

                        auto response = co_await requests->get(*url);
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
                    [](auto l) -> asyncio::task::Task<void> {
                        using namespace std::string_view_literals;

                        auto stream = co_await l.accept().transform([](asyncio::net::TCPStream &&value) {
                            return std::make_shared<asyncio::net::TCPStream>(std::move(value));
                        });
                        REQUIRE(stream);

                        asyncio::BufReader reader(*stream);

                        auto line = co_await reader.readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "GET /object?id=0 HTTP/1.1");

                        while (true) {
                            line = co_await reader.readLine();
                            REQUIRE(line);

                            if (line->empty())
                                break;
                        }

                        const auto res = co_await stream.value()->writeAll(
                            std::as_bytes(
                                std::span{
                                    "HTTP/1.1 200 OK\r\n"
                                    "Content-Length: 0\r\n\r\n"sv
                                }
                            )
                        );
                        REQUIRE(res);
                    }(*std::move(listener)),
                    []() -> asyncio::task::Task<void> {
                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        auto requests = asyncio::http::Requests::make();
                        REQUIRE(requests);

                        auto response = co_await requests->get(*url);
                        REQUIRE(response);

                        const auto content = co_await response->string();
                        REQUIRE(content);
                        REQUIRE(content->empty());
                    }()
                );
            }

            SECTION("get json") {
                co_await allSettled(
                    [](auto l) -> asyncio::task::Task<void> {
                        using namespace std::string_view_literals;

                        auto stream = co_await l.accept().transform([](asyncio::net::TCPStream &&value) {
                            return std::make_shared<asyncio::net::TCPStream>(std::move(value));
                        });
                        REQUIRE(stream);

                        asyncio::BufReader reader(*stream);

                        auto line = co_await reader.readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "GET /object?id=0 HTTP/1.1");

                        while (true) {
                            line = co_await reader.readLine();
                            REQUIRE(line);

                            if (line->empty())
                                break;
                        }

                        const auto res = co_await stream.value()->writeAll(
                            std::as_bytes(
                                std::span{
                                    "HTTP/1.1 200 OK\r\n"
                                    "Content-Length: 27\r\n\r\n"
                                    "{\"name\": \"rose\", \"age\": 17}"sv
                                }
                            )
                        );
                        REQUIRE(res);
                    }(*std::move(listener)),
                    []() -> asyncio::task::Task<void> {
                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        auto requests = asyncio::http::Requests::make();
                        REQUIRE(requests);

                        auto response = co_await requests->get(*url);
                        REQUIRE(response);

                        const auto people = co_await response->string().transform([](const auto &content) {
                            return nlohmann::json::parse(content).template get<People>();
                        });
                        REQUIRE(people);
                        REQUIRE(people->name == "rose");
                        REQUIRE(people->age == 17);
                    }()
                );
            }
        }

        SECTION("post") {
            SECTION("post string") {
                co_await allSettled(
                    [](auto l) -> asyncio::task::Task<void> {
                        using namespace std::string_view_literals;

                        auto stream = co_await l.accept().transform([](asyncio::net::TCPStream &&value) {
                            return std::make_shared<asyncio::net::TCPStream>(std::move(value));
                        });
                        REQUIRE(stream);

                        asyncio::BufReader reader(*stream);

                        auto line = co_await reader.readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "POST /object?id=0 HTTP/1.1");

                        std::map<std::string, std::string> headers;

                        while (true) {
                            line = co_await reader.readLine();
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

                        std::string content;
                        content.resize(*length);

                        auto res = co_await reader.readExactly(std::as_writable_bytes(std::span{content}));
                        REQUIRE(res);
                        REQUIRE(content == "hello world");

                        res = co_await stream.value()->writeAll(
                            std::as_bytes(
                                std::span{
                                    "HTTP/1.1 200 OK\r\n"
                                    "Content-Length: 11\r\n\r\n"
                                    "hello world"sv
                                }
                            )
                        );
                        REQUIRE(res);
                    }(*std::move(listener)),
                    []() -> asyncio::task::Task<void> {
                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        auto requests = asyncio::http::Requests::make();
                        REQUIRE(requests);

                        auto response = co_await requests->post(*url, "hello world");
                        REQUIRE(response);

                        const auto content = co_await response->string();
                        REQUIRE(content);
                        REQUIRE(*content == "hello world");
                    }()
                );
            }

            SECTION("post form") {
                co_await allSettled(
                    [](auto l) -> asyncio::task::Task<void> {
                        using namespace std::string_view_literals;

                        auto stream = co_await l.accept().transform([](asyncio::net::TCPStream &&value) {
                            return std::make_shared<asyncio::net::TCPStream>(std::move(value));
                        });
                        REQUIRE(stream);

                        asyncio::BufReader reader(*stream);

                        auto line = co_await reader.readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "POST /object?id=0 HTTP/1.1");

                        std::map<std::string, std::string> headers;

                        while (true) {
                            line = co_await reader.readLine();
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

                        std::string content;
                        content.resize(*length);

                        auto res = co_await reader.readExactly(std::as_writable_bytes(std::span{content}));
                        REQUIRE(res);
                        REQUIRE(content == "name=jack");

                        res = co_await stream.value()->writeAll(
                            std::as_bytes(
                                std::span{
                                    "HTTP/1.1 200 OK\r\n"
                                    "Content-Length: 11\r\n\r\n"
                                    "hello world"sv
                                }
                            )
                        );
                        REQUIRE(res);
                    }(*std::move(listener)),
                    []() -> asyncio::task::Task<void> {
                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        auto requests = asyncio::http::Requests::make();
                        REQUIRE(requests);

                        const std::map<std::string, std::string> payload{{"name", "jack"}};
                        auto response = co_await requests->post(*url, payload);
                        REQUIRE(response);

                        const auto content = co_await response->string();
                        REQUIRE(content);
                        REQUIRE(*content == "hello world");
                    }()
                );
            }

            SECTION("post file") {
                co_await allSettled(
                    [](auto l) -> asyncio::task::Task<void> {
                        using namespace std::string_view_literals;

                        auto stream = co_await l.accept().transform([](asyncio::net::TCPStream &&value) {
                            return std::make_shared<asyncio::net::TCPStream>(std::move(value));
                        });
                        REQUIRE(stream);

                        asyncio::BufReader reader(*stream);

                        auto line = co_await reader.readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "POST /object?id=0 HTTP/1.1");

                        std::map<std::string, std::string> headers;

                        while (true) {
                            line = co_await reader.readLine();
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

                        std::string content;
                        content.resize(*length);

                        auto res = co_await reader.readExactly(std::as_writable_bytes(std::span{content}));
                        REQUIRE(res);

                        res = co_await stream.value()->writeAll(
                            std::as_bytes(
                                std::span{
                                    "HTTP/1.1 200 OK\r\n"
                                    "Content-Length: 11\r\n\r\n"
                                    "hello world"sv
                                }
                            )
                        );
                        REQUIRE(res);
                    }(*std::move(listener)),
                    []() -> asyncio::task::Task<void> {
                        const auto path = std::filesystem::temp_directory_path() / "asyncio-requests";
                        REQUIRE(zero::filesystem::write(path, "hello world"));

                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        auto requests = asyncio::http::Requests::make();
                        REQUIRE(requests);

                        const std::map<std::string, std::filesystem::path> payload{{"file", path}};
                        auto response = co_await requests->post(*url, payload);
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
                    [](auto l) -> asyncio::task::Task<void> {
                        using namespace std::string_view_literals;

                        auto stream = co_await l.accept().transform([](asyncio::net::TCPStream &&value) {
                            return std::make_shared<asyncio::net::TCPStream>(std::move(value));
                        });
                        REQUIRE(stream);

                        asyncio::BufReader reader(*stream);

                        auto line = co_await reader.readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "POST /object?id=0 HTTP/1.1");

                        std::map<std::string, std::string> headers;

                        while (true) {
                            line = co_await reader.readLine();
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

                        std::string content;
                        content.resize(*length);

                        auto res = co_await reader.readExactly(std::as_writable_bytes(std::span{content}));
                        REQUIRE(res);

                        res = co_await stream.value()->writeAll(
                            std::as_bytes(
                                std::span{
                                    "HTTP/1.1 200 OK\r\n"
                                    "Content-Length: 11\r\n\r\n"
                                    "hello world"sv
                                }
                            )
                        );
                        REQUIRE(res);
                    }(*std::move(listener)),
                    []() -> asyncio::task::Task<void> {
                        const auto path = std::filesystem::temp_directory_path() / "asyncio-requests";
                        REQUIRE(zero::filesystem::write(path, "hello world"));

                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        auto requests = asyncio::http::Requests::make();
                        REQUIRE(requests);

                        const std::map<std::string, std::variant<std::string, std::filesystem::path>> payload{
                            {"name", std::string{"jack"}},
                            {"file", path}
                        };

                        auto response = co_await requests->post(*url, payload);
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
                    [](auto l) -> asyncio::task::Task<void> {
                        using namespace std::string_view_literals;

                        auto stream = co_await l.accept().transform([](asyncio::net::TCPStream &&value) {
                            return std::make_shared<asyncio::net::TCPStream>(std::move(value));
                        });
                        REQUIRE(stream);

                        asyncio::BufReader reader(*stream);

                        auto line = co_await reader.readLine();
                        REQUIRE(line);
                        REQUIRE(*line == "POST /object?id=0 HTTP/1.1");

                        std::map<std::string, std::string> headers;

                        while (true) {
                            line = co_await reader.readLine();
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

                        std::string content;
                        content.resize(*length);

                        auto res = co_await reader.readExactly(std::as_writable_bytes(std::span{content}));
                        REQUIRE(res);

                        const auto [name, age] = nlohmann::json::parse(content).get<People>();
                        REQUIRE(name == "jack");
                        REQUIRE(age == 18);

                        res = co_await stream.value()->writeAll(
                            std::as_bytes(
                                std::span{
                                    "HTTP/1.1 200 OK\r\n"
                                    "Content-Length: 11\r\n\r\n"
                                    "hello world"sv
                                }
                            )
                        );
                        REQUIRE(res);
                    }(*std::move(listener)),
                    []() -> asyncio::task::Task<void> {
                        const auto url = asyncio::http::URL::from(URL);
                        REQUIRE(url);

                        auto requests = asyncio::http::Requests::make();
                        REQUIRE(requests);

                        auto response = co_await requests->post(*url, People{"jack", 18});
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
                [](auto l) -> asyncio::task::Task<void> {
                    using namespace std::string_view_literals;

                    auto stream = co_await l.accept().transform([](asyncio::net::TCPStream &&value) {
                        return std::make_shared<asyncio::net::TCPStream>(std::move(value));
                    });
                    REQUIRE(stream);

                    asyncio::BufReader reader(*stream);

                    auto line = co_await reader.readLine();
                    REQUIRE(line);
                    REQUIRE(*line == "GET /object?id=0 HTTP/1.1");

                    while (true) {
                        line = co_await reader.readLine();
                        REQUIRE(line);

                        if (line->empty())
                            break;
                    }

                    const auto res = co_await stream.value()->writeAll(
                        std::as_bytes(
                            std::span{
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Length: 11\r\n\r\n"
                                "hello world"sv
                            }
                        )
                    );
                    REQUIRE(res);
                }(*std::move(listener)),
                []() -> asyncio::task::Task<void> {
                    const auto url = asyncio::http::URL::from(URL);
                    REQUIRE(url);

                    auto requests = asyncio::http::Requests::make();
                    REQUIRE(requests);

                    auto response = co_await requests->get(*url);
                    REQUIRE(response);

                    const auto path = std::filesystem::temp_directory_path() / "asyncio-requests";
                    const auto res = co_await response->output(path);
                    REQUIRE(res);

                    const auto content = zero::filesystem::readString(path);
                    REQUIRE(content);
                    REQUIRE(*content == "hello world");

                    REQUIRE(std::filesystem::remove(path));
                }()
            );
        }
    });
    REQUIRE(result);
    REQUIRE(*result);
}

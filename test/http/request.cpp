#include <asyncio/http/request.h>
#include <asyncio/event_loop.h>
#include <asyncio/net/stream.h>
#include <catch2/catch_test_macros.hpp>
#include <fstream>

TEST_CASE("http requests", "[request]") {
    SECTION("get") {
        SECTION("normal") {
            asyncio::run([&]() -> zero::async::coroutine::Task<void> {
                auto listener = asyncio::net::stream::listen("127.0.0.1", 30000);
                REQUIRE(listener);

                co_await zero::async::coroutine::allSettled(
                        [](auto listener) -> zero::async::coroutine::Task<void> {
                            auto buffer = std::move(co_await listener.accept());
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

                            std::string response = "HTTP/1.1 200 OK\r\n"
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
                            auto url = asyncio::http::URL::from("http://localhost:30000/object?id=0");
                            REQUIRE(url);

                            auto result = asyncio::http::makeRequests();
                            REQUIRE(result);

                            auto &requests = *result;
                            auto response = std::move(co_await requests->get(*url));
                            REQUIRE(response);

                            REQUIRE(response->statusCode() == 200);

                            auto length = response->contentLength();
                            REQUIRE(length);
                            REQUIRE(*length == 11);

                            auto type = response->contentType();
                            REQUIRE(type);
                            REQUIRE(*type == "text/html");

                            auto server = response->header("Server");
                            REQUIRE(server);
                            REQUIRE(*server == "asyncio");

                            auto cookies = response->cookies();
                            REQUIRE(cookies.size() == 1);
                            REQUIRE(cookies[0].find("jack") != std::string::npos);

                            auto content = co_await response->string();
                            REQUIRE(content);
                            REQUIRE(*content == "hello world");
                        }()
                );
            });
        }

        SECTION("empty body") {
            asyncio::run([&]() -> zero::async::coroutine::Task<void> {
                auto listener = asyncio::net::stream::listen("127.0.0.1", 30000);
                REQUIRE(listener);

                co_await zero::async::coroutine::allSettled(
                        [](auto listener) -> zero::async::coroutine::Task<void> {
                            auto buffer = std::move(co_await listener.accept());
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

                            std::string response = "HTTP/1.1 200 OK\r\n"
                                                   "Content-Length: 0\r\n\r\n";

                            auto result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                            REQUIRE(result);

                            result = co_await buffer->flush();
                            REQUIRE(result);
                        }(std::move(*listener)),
                        []() -> zero::async::coroutine::Task<void> {
                            auto url = asyncio::http::URL::from("http://localhost:30000/object?id=0");
                            REQUIRE(url);

                            auto result = asyncio::http::makeRequests();
                            REQUIRE(result);

                            auto &requests = *result;
                            auto response = std::move(co_await requests->get(*url));
                            REQUIRE(response);

                            auto content = co_await response->string();
                            REQUIRE(content);
                            REQUIRE(content->empty());
                        }()
                );
            });
        }
    }

    SECTION("post") {
        SECTION("post form") {
            asyncio::run([&]() -> zero::async::coroutine::Task<void> {
                auto listener = asyncio::net::stream::listen("127.0.0.1", 30000);
                REQUIRE(listener);

                co_await zero::async::coroutine::allSettled(
                        [](auto listener) -> zero::async::coroutine::Task<void> {
                            auto buffer = std::move(co_await listener.accept());
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

                            auto it = headers.find("Content-Length");
                            REQUIRE(it != headers.end());

                            auto length = zero::strings::toNumber<size_t>(it->second);
                            REQUIRE(length);

                            std::vector<std::byte> data(*length);
                            co_await buffer->readExactly(data);
                            REQUIRE(memcmp(data.data(), "name=jack", 9) == 0);

                            std::string response = "HTTP/1.1 200 OK\r\n"
                                                   "Content-Length: 11\r\n\r\n"
                                                   "hello world";

                            auto result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                            REQUIRE(result);

                            result = co_await buffer->flush();
                            REQUIRE(result);
                        }(std::move(*listener)),
                        []() -> zero::async::coroutine::Task<void> {
                            auto url = asyncio::http::URL::from("http://localhost:30000/object?id=0");
                            REQUIRE(url);

                            auto result = asyncio::http::makeRequests();
                            REQUIRE(result);

                            auto &requests = *result;
                            std::map<std::string, std::string> payload{{"name", "jack"}};

                            auto response = std::move(co_await requests->post(*url, payload));
                            REQUIRE(response);

                            auto content = co_await response->string();
                            REQUIRE(content);
                            REQUIRE(*content == "hello world");
                        }()
                );
            });
        }

        SECTION("post file") {
            asyncio::run([&]() -> zero::async::coroutine::Task<void> {
                auto listener = asyncio::net::stream::listen("127.0.0.1", 30000);
                REQUIRE(listener);

                co_await zero::async::coroutine::allSettled(
                        [](auto listener) -> zero::async::coroutine::Task<void> {
                            auto buffer = std::move(co_await listener.accept());
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

                            auto it = headers.find("Content-Length");
                            REQUIRE(it != headers.end());

                            auto length = zero::strings::toNumber<size_t>(it->second);
                            REQUIRE(length);

                            std::vector<std::byte> data(*length);
                            co_await buffer->readExactly(data);

                            std::string response = "HTTP/1.1 200 OK\r\n"
                                                   "Content-Length: 11\r\n\r\n"
                                                   "hello world";

                            auto result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                            REQUIRE(result);

                            result = co_await buffer->flush();
                            REQUIRE(result);
                        }(std::move(*listener)),
                        []() -> zero::async::coroutine::Task<void> {
                            auto path = std::filesystem::temp_directory_path() / "asyncio-file";
                            std::ofstream stream(path);
                            REQUIRE(stream.is_open());

                            stream << "hello world";
                            stream.close();

                            auto url = asyncio::http::URL::from("http://localhost:30000/object?id=0");
                            REQUIRE(url);

                            auto result = asyncio::http::makeRequests();
                            REQUIRE(result);

                            auto &requests = *result;
                            std::map<std::string, std::filesystem::path> payload{{"file", path}};

                            auto response = std::move(co_await requests->post(*url, payload));
                            REQUIRE(response);

                            auto content = co_await response->string();
                            REQUIRE(content);
                            REQUIRE(*content == "hello world");

                            std::filesystem::remove(path);
                        }()
                );
            });
        }

        SECTION("post multipart") {
            asyncio::run([&]() -> zero::async::coroutine::Task<void> {
                auto listener = asyncio::net::stream::listen("127.0.0.1", 30000);
                REQUIRE(listener);

                co_await zero::async::coroutine::allSettled(
                        [](auto listener) -> zero::async::coroutine::Task<void> {
                            auto buffer = std::move(co_await listener.accept());
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

                            auto it = headers.find("Content-Length");
                            REQUIRE(it != headers.end());

                            auto length = zero::strings::toNumber<size_t>(it->second);
                            REQUIRE(length);

                            std::vector<std::byte> data(*length);
                            co_await buffer->readExactly(data);

                            std::string response = "HTTP/1.1 200 OK\r\n"
                                                   "Content-Length: 11\r\n\r\n"
                                                   "hello world";

                            auto result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                            REQUIRE(result);

                            result = co_await buffer->flush();
                            REQUIRE(result);
                        }(std::move(*listener)),
                        []() -> zero::async::coroutine::Task<void> {
                            auto path = std::filesystem::temp_directory_path() / "asyncio-file";
                            std::ofstream stream(path);
                            REQUIRE(stream.is_open());

                            stream << "hello world";
                            stream.close();

                            auto url = asyncio::http::URL::from("http://localhost:30000/object?id=0");
                            REQUIRE(url);

                            auto result = asyncio::http::makeRequests();
                            REQUIRE(result);

                            auto &requests = *result;
                            std::map<std::string, std::variant<std::string, std::filesystem::path>> payload{
                                    {"name", std::string{"jack"}},
                                    {"file", path}
                            };

                            auto response = std::move(co_await requests->post(*url, payload));
                            REQUIRE(response);

                            auto content = co_await response->string();
                            REQUIRE(content);
                            REQUIRE(*content == "hello world");

                            std::filesystem::remove(path);
                        }()
                );
            });
        }

        SECTION("post json") {
            asyncio::run([&]() -> zero::async::coroutine::Task<void> {
                auto listener = asyncio::net::stream::listen("127.0.0.1", 30000);
                REQUIRE(listener);

                co_await zero::async::coroutine::allSettled(
                        [](auto listener) -> zero::async::coroutine::Task<void> {
                            auto buffer = std::move(co_await listener.accept());
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

                            auto length = zero::strings::toNumber<size_t>(it->second);
                            REQUIRE(length);

                            std::vector<std::byte> data(*length);
                            co_await buffer->readExactly(data);

                            auto j = nlohmann::json::parse(data);
                            REQUIRE(j["name"] == "jack");
                            REQUIRE(j["age"] == 12);

                            std::string response = "HTTP/1.1 200 OK\r\n"
                                                   "Content-Length: 11\r\n\r\n"
                                                   "hello world";

                            auto result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                            REQUIRE(result);

                            result = co_await buffer->flush();
                            REQUIRE(result);
                        }(std::move(*listener)),
                        []() -> zero::async::coroutine::Task<void> {
                            auto url = asyncio::http::URL::from("http://localhost:30000/object?id=0");
                            REQUIRE(url);

                            auto result = asyncio::http::makeRequests();
                            REQUIRE(result);

                            auto &requests = *result;
                            nlohmann::json payload{
                                    {"name", "jack"},
                                    {"age",  12}
                            };

                            auto response = std::move(co_await requests->post(*url, payload));
                            REQUIRE(response);

                            auto content = co_await response->string();
                            REQUIRE(content);
                            REQUIRE(*content == "hello world");
                        }()
                );
            });
        }
    }

    SECTION("output to file") {
        asyncio::run([&]() -> zero::async::coroutine::Task<void> {
            auto listener = asyncio::net::stream::listen("127.0.0.1", 30000);
            REQUIRE(listener);

            co_await zero::async::coroutine::allSettled(
                    [](auto listener) -> zero::async::coroutine::Task<void> {
                        auto buffer = std::move(co_await listener.accept());
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

                        std::string response = "HTTP/1.1 200 OK\r\n"
                                               "Content-Length: 11\r\n\r\n"
                                               "hello world";

                        auto result = co_await buffer->writeAll(std::as_bytes(std::span{response}));
                        REQUIRE(result);

                        result = co_await buffer->flush();
                        REQUIRE(result);
                    }(std::move(*listener)),
                    []() -> zero::async::coroutine::Task<void> {
                        auto url = asyncio::http::URL::from("http://localhost:30000/object?id=0");
                        REQUIRE(url);

                        auto result = asyncio::http::makeRequests();
                        REQUIRE(result);

                        auto &requests = *result;
                        auto response = std::move(co_await requests->get(*url));
                        REQUIRE(response);

                        auto path = std::filesystem::temp_directory_path() / "asyncio-file";
                        auto res = co_await response->output(path);
                        REQUIRE(res);

                        std::ifstream stream(path);
                        REQUIRE(stream.is_open());

                        std::string content = {
                                std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>()
                        };

                        REQUIRE(content == "hello world");

                        stream.close();
                        std::filesystem::remove(path);
                    }()
            );
        });
    }
}
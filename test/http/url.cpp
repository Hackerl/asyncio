#include <asyncio/http/url.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("http url", "[http]") {
    SECTION("parse") {
        REQUIRE(!asyncio::http::URL::from(":/qq.com"));

        const auto url = asyncio::http::URL::from("https://root:123456@localhost/file%20manager/abc?name=jack&age=12");
        REQUIRE(url);

        const auto scheme = url->scheme();
        REQUIRE(scheme);
        REQUIRE(*scheme == "https");

        const auto user = url->user();
        REQUIRE(user);
        REQUIRE(*user == "root");

        const auto password = url->password();
        REQUIRE(password);
        REQUIRE(*password == "123456");

        const auto host = url->host();
        REQUIRE(host);
        REQUIRE(*host == "localhost");

        const auto path = url->path();
        REQUIRE(path);
        REQUIRE(*path == "/file manager/abc");

        const auto query = url->query();
        REQUIRE(query);
        REQUIRE(*query == "name=jack&age=12");
    }

    asyncio::http::URL url;
    REQUIRE(!url.string());

    SECTION("modify") {
        url.scheme("https")
           .host("localhost")
           .user("root")
           .password("123456")
           .path("/file/abc")
           .query("name=jack&age=12");

        auto u = url.string();
        REQUIRE(u);
        REQUIRE(*u == "https://root:123456@localhost/file/abc?name=jack&age=12");
        REQUIRE(fmt::to_string(url) == "expected(https://root:123456@localhost/file/abc?name=jack&age=12)");

        url.scheme("https")
           .host("example.com")
           .port(444)
           .user("root")
           .password("123456")
           .path("/file")
           .append("bcd")
           .query(std::nullopt)
           .appendQuery("name", "michael jackson")
           .appendQuery("age", 12);

        u = url.string();
        REQUIRE(u);
        REQUIRE(*u == "https://root:123456@example.com:444/file/bcd?name=michael+jackson&age=12");
        REQUIRE(
            fmt::to_string(url) == "expected(https://root:123456@example.com:444/file/bcd?name=michael+jackson&age=12)")
        ;

        url.host("127.0.0.1")
           .port(std::nullopt)
           .user(std::nullopt);

        u = url.string();
        REQUIRE(u);
        REQUIRE(*u == "https://:123456@127.0.0.1/file/bcd?name=michael+jackson&age=12");
        REQUIRE(fmt::to_string(url) == "expected(https://:123456@127.0.0.1/file/bcd?name=michael+jackson&age=12)");

        url.password(std::nullopt)
           .path(std::nullopt)
           .query(std::nullopt);

        u = url.string();
        REQUIRE(u);
        REQUIRE(*u == "https://127.0.0.1/");
        REQUIRE(fmt::to_string(url) == "expected(https://127.0.0.1/)");
    }
}

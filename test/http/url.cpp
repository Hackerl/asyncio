#include <asyncio/http/url.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("http url", "[http]") {
    SECTION("parse") {
        REQUIRE(!asyncio::http::URL::from(":/qq.com"));

        const auto url = asyncio::http::URL::from("https://root:123456@localhost/file/abc?name=jack&age=12");
        REQUIRE(url);

        REQUIRE(*url->scheme() == "https");
        REQUIRE(*url->user() == "root");
        REQUIRE(*url->password() == "123456");
        REQUIRE(*url->host() == "localhost");
        REQUIRE(*url->path() == "/file/abc");
        REQUIRE(*url->query() == "name=jack&age=12");
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

        REQUIRE(*url.string() == "https://root:123456@localhost/file/abc?name=jack&age=12");

        url.scheme("https")
           .host("example.com")
           .port(444)
           .user("root")
           .password("123456")
           .path("/file")
           .append("bcd")
           .query(std::nullopt)
           .appendQuery("name", "jack")
           .appendQuery("age", 12);

        REQUIRE(*url.string() == "https://root:123456@example.com:444/file/bcd?name=jack&age=12");

        url.host("127.0.0.1")
           .port(std::nullopt)
           .user(std::nullopt);

        REQUIRE(*url.string() == "https://:123456@127.0.0.1/file/bcd?name=jack&age=12");

        url.password(std::nullopt)
           .path(std::nullopt)
           .query(std::nullopt);

        REQUIRE(*url.string() == "https://127.0.0.1/");
    }
}

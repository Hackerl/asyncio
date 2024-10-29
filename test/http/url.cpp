#include <catch_extensions.h>
#include <asyncio/http/url.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("URL escape", "[http]") {
    REQUIRE(
        asyncio::http::urlEscape(R"("测试&hello world!@#$%^&*()_+[]{}|;':\",.<>?")") ==
        "%22%E6%B5%8B%E8%AF%95%26hello%20world%21%40%23%24%25%5E%26%2A%28%29_"
        "%2B%5B%5D%7B%7D%7C%3B%27%3A%5C%22%2C.%3C%3E%3F%22"
    );
}

TEST_CASE("URL unescape", "[http]") {
    REQUIRE(
        asyncio::http::urlUnescape(
            "%22%E6%B5%8B%E8%AF%95%26hello%20world%21%40%23%24%25%5E%26%2A%28%29_"
            "%2B%5B%5D%7B%7D%7C%3B%27%3A%5C%22%2C.%3C%3E%3F%22"
        ) == R"("测试&hello world!@#$%^&*()_+[]{}|;':\",.<>?")"
    );
}

TEST_CASE("URL", "[http]") {
    auto url = asyncio::http::URL::from("http://root:123456@localhost:8080/login?name=rose#page=1");
    REQUIRE(url);

    SECTION("scheme") {
        SECTION("get") {
            REQUIRE(url->scheme() == "http");
        }

        SECTION("set") {
            url->scheme("https");
            REQUIRE(url->scheme() == "https");
        }
    }

    SECTION("user") {
        SECTION("get") {
            REQUIRE(url->user() == "root");
        }

        SECTION("set") {
            url->user("admin");
            REQUIRE(url->user() == "admin");
        }

        SECTION("reset") {
            url->user(std::nullopt);
            REQUIRE_FALSE(url->user());
        }
    }

    SECTION("password") {
        SECTION("get") {
            REQUIRE(url->password() == "123456");
        }

        SECTION("set") {
            url->password("admin");
            REQUIRE(url->password() == "admin");
        }

        SECTION("reset") {
            url->password(std::nullopt);
            REQUIRE_FALSE(url->password());
        }
    }

    SECTION("host") {
        SECTION("get") {
            REQUIRE(url->host() == "localhost");
        }

        SECTION("set") {
            url->host("127.0.0.1");
            REQUIRE(url->host() == "127.0.0.1");
        }

        SECTION("reset") {
            url->host(std::nullopt);
            REQUIRE_FALSE(url->host());
        }
    }

    SECTION("port") {
        SECTION("get") {
            REQUIRE(url->port() == 8080);
        }

        SECTION("set") {
            url->port(1080);
            REQUIRE(url->port() == 1080);
        }

        SECTION("reset") {
            url->port(std::nullopt);

            SECTION("default port") {
                REQUIRE(url->port() == 80);
            }

            SECTION("no default port") {
                url->scheme("file");
                REQUIRE_FALSE(url->port());
            }
        }
    }

    SECTION("path") {
        SECTION("get") {
            REQUIRE(url->path() == "/login");
            REQUIRE(url->rawPath() == url->path());
        }

        SECTION("set") {
            SECTION("unencoded") {
                url->path("/logout");
                REQUIRE(url->path() == "/logout");
            }

            SECTION("encoded") {
                url->path(R"(/test/路径/with spaces/and!@#$%^&*()_+-=[]{}|;':",./<>?)");
                REQUIRE(url->path() == R"(/test/路径/with spaces/and!@#$%^&*()_+-=[]{}|;':",./<>?)");
                REQUIRE(
                    url->rawPath() ==
                    "/test/%e8%b7%af%e5%be%84/with%20spaces"
                    "/and%21%40%23%24%25%5e%26%2a%28%29_%2b-%3d%5b%5d%7b%7d%7c%3b%27%3a%22%2c./%3c%3e%3f"
                );
            }
        }

        SECTION("append") {
            url->path("/");

            SECTION("normal") {
                url->append("api");
                REQUIRE(url->path() == "/api");

                url->append("login/");
                REQUIRE(url->path() == "/api/login/");
            }

            SECTION("number") {
                url->append("id");
                REQUIRE(url->path() == "/id");

                url->append(100);
                REQUIRE(url->path() == "/id/100");
            }
        }
    }

    SECTION("query") {
        SECTION("get") {
            REQUIRE(url->query() == "name=rose");
            REQUIRE(url->rawQuery() == "name=rose");
        }

        SECTION("set") {
            url->query("name=jack");
            REQUIRE(url->query() == "name=jack");
            REQUIRE(url->rawQuery() == "name=jack");
        }

        SECTION("append") {
            url->query(std::nullopt);

            SECTION("overloading") {
                SECTION("entry") {
                    url->appendQuery("name=jack");
                    REQUIRE(url->query() == "name=jack");

                    url->appendQuery("age=18");
                    REQUIRE(url->query() == "name=jack&age=18");
                }

                SECTION("kv pair") {
                    SECTION("string") {
                        url->appendQuery("name", "jack");
                        REQUIRE(url->query() == "name=jack");

                        url->appendQuery("sex", "male");
                        REQUIRE(url->query() == "name=jack&sex=male");
                    }

                    SECTION("boolean") {
                        url->appendQuery("adult", true);
                        REQUIRE(url->query() == "adult=true");

                        url->appendQuery("single", false);
                        REQUIRE(url->query() == "adult=true&single=false");
                    }

                    SECTION("number") {
                        url->appendQuery("age", 18);
                        REQUIRE(url->query() == "age=18");

                        url->appendQuery("height", 180);
                        REQUIRE(url->query() == "age=18&height=180");
                    }
                }
            }

            SECTION("encoded") {
                url->appendQuery("name", "测试");
                REQUIRE(url->query() == "name=测试");
                REQUIRE(url->rawQuery() == "name=%e6%b5%8b%e8%af%95");

                url->appendQuery("description", R"(special chars !@#$%^&*()_+-=[]{}|;':",./<>? and spaces)");
                REQUIRE(url->query() == R"(name=测试&description=special chars !@#$%^&*()_+-=[]{}|;':",./<>? and spaces)")
                ;
                REQUIRE(
                    url->rawQuery() ==
                    "name=%e6%b5%8b%e8%af%95&description=special+chars+%21%40%23%24%25%5e%26%2a%28%29_"
                    "%2b-%3d%5b%5d%7b%7d%7c%3b%27%3a%22%2c.%2f%3c%3e%3f+and+spaces"
                );
            }
        }

        SECTION("reset") {
            url->query(std::nullopt);
            REQUIRE_FALSE(url->query());
        }
    }

    SECTION("fragment") {
        SECTION("get") {
            REQUIRE(url->fragment() == "page=1");
        }

        SECTION("set") {
            url->fragment("page=2");
            REQUIRE(url->fragment() == "page=2");
        }

        SECTION("reset") {
            url->fragment(std::nullopt);
            REQUIRE_FALSE(url->fragment());
        }
    }

    SECTION("string") {
        REQUIRE(url->string() == "http://root:123456@localhost:8080/login?name=rose#page=1");
    }
}

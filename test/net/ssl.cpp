#include <asyncio/net/ssl.h>
#include <asyncio/event_loop.h>
#include <zero/strings/strings.h>
#include <catch2/catch_test_macros.hpp>
#include <fmt/std.h>

constexpr auto CA_CERT = "-----BEGIN CERTIFICATE-----\n"
    "MIIDYTCCAkkCFF5tqhRQzORYNN/5dVTYVMORdAZUMA0GCSqGSIb3DQEBCwUAMG0x\n"
    "CzAJBgNVBAYTAkNOMRMwEQYDVQQIDApteXByb3ZpbmNlMQ8wDQYDVQQHDAZteWNp\n"
    "dHkxFzAVBgNVBAoMDm15b3JnYW5pemF0aW9uMRAwDgYDVQQLDAdteWdyb3VwMQ0w\n"
    "CwYDVQQDDARteUNBMB4XDTIzMDYxNjA1NDQwMFoXDTI0MDYxNTA1NDQwMFowbTEL\n"
    "MAkGA1UEBhMCQ04xEzARBgNVBAgMCm15cHJvdmluY2UxDzANBgNVBAcMBm15Y2l0\n"
    "eTEXMBUGA1UECgwObXlvcmdhbml6YXRpb24xEDAOBgNVBAsMB215Z3JvdXAxDTAL\n"
    "BgNVBAMMBG15Q0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCvK6zh\n"
    "ERlUEalTh8+Duge7RrlzluG2WMCJa+NfkST6/t2LUuOGtpDrsanmeqT7U2hbuOFj\n"
    "zreaINUdO58FUVBWfa++y68l6HI6j1rCWNAZRF6Fwt8LZfHR/4BUbDNv+w6ZC9eE\n"
    "4h6FZvcN1WYxgTP9MQhxpcqZrmr8PlRq1Td/OUrT309xfVGtsU+Yap7MUN2knzmR\n"
    "MVcyyjsRHRaxT20di3nik4gTEoiw+t5bmGiz4Qq+5x9Rnn7m32cmkQnf629TpKuK\n"
    "EmSeGA3xENhSFzROXLsohNbKvXTiHc2O4RE76niu9oLl3mRUv22MNcoytkj8tBvq\n"
    "TqlpucsLB9crtRIdAgMBAAEwDQYJKoZIhvcNAQELBQADggEBAHMRclABYi7r8nCw\n"
    "TQw8dMN7Z2msbdCzcS3JB4zSpmGjpfC1fpU5aiCGApRXHNicl988Ysn1t5ltrLrs\n"
    "7OJi0B8YwnXqX3lqBYh6rpVXadswQ35vomrsRdU+rPqxQFDqgjEhq5TIrJ+woZG3\n"
    "dAgrVeLuTPAiL0nxGDn/zScbwFKT7lpNjsjNLja6ySFaxRkuNNBnpYDoM121R5Pc\n"
    "dSxE88zWMKABl5+QS6QEfEhHrRGinjpwwc1PM9wTxdv8FT/voWwTNH/6bKRxZOzJ\n"
    "rXEx91tY03rGk3zeHBnMod3ygF8Y3LdCYcIxoveKOKDycuGBQ8MFebrboPFjId+i\n"
    "u3X8p/I=\n"
    "-----END CERTIFICATE-----";

constexpr auto SERVER_CERT = "-----BEGIN CERTIFICATE-----\n"
    "MIIDZjCCAk4CFGH1jGrWX7511SW8Wl4e1hT098eaMA0GCSqGSIb3DQEBCwUAMG0x\n"
    "CzAJBgNVBAYTAkNOMRMwEQYDVQQIDApteXByb3ZpbmNlMQ8wDQYDVQQHDAZteWNp\n"
    "dHkxFzAVBgNVBAoMDm15b3JnYW5pemF0aW9uMRAwDgYDVQQLDAdteWdyb3VwMQ0w\n"
    "CwYDVQQDDARteUNBMB4XDTIzMDYxNjA1NDQwMFoXDTI0MDYxNTA1NDQwMFowcjEL\n"
    "MAkGA1UEBhMCQ04xEzARBgNVBAgMCm15cHJvdmluY2UxDzANBgNVBAcMBm15Y2l0\n"
    "eTEXMBUGA1UECgwObXlvcmdhbml6YXRpb24xEDAOBgNVBAsMB215Z3JvdXAxEjAQ\n"
    "BgNVBAMMCWxvY2FsaG9zdDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB\n"
    "ALRnkdFTVkQSJJZeBk4jdAcj564FsYnrgUvE3azMFSvCqN9MXlhpUDyFdghJRkMS\n"
    "nSIYBg50M9g8NKdy5QbGu0ho1p7tSxTanft9eRXWtlgdlhBD5OlYb7gdqAUH1r4A\n"
    "KkE9CiMkEVcA/bGZ3UgC2G6TzaXk0YCFTxNtJQRAmJqX0o8bdgzpYnLek5U0wvt2\n"
    "HDe1HPL/4ZTvkW+baQmhuZ30zdG1KowbwT406z1so2H8C73QlFLvv4UK9mfKaAIt\n"
    "bNKiB+oZ062LDgBQZWS7mu8dsujiPxIw+PNIvCCED7JmyhanhmRcqhbwv3pjCtu8\n"
    "bjtNZyEUqQAob3iw4TSOHmcCAwEAATANBgkqhkiG9w0BAQsFAAOCAQEADVvk9jE6\n"
    "7h1+8afPH7XauVlSsMbplTrq8itExi5T7u9ugrLLzYcjB7SkHFcGCbiILw/Zmslw\n"
    "r/xzAoWVw7xuhoPU+lLCYw7iL4NO4T1aKEE4EGU1TG49qWsItGeneQq69T6gy0RY\n"
    "0oNUD+VMCpffltnoWPPIcE0jDk+gE0Lt7UPaYtC+tDb/0xb4RGSCC2Yj2eZcylHx\n"
    "2uEjpPt5yb1dvqqyGm0izVulVqdnQMglyVhas12ZfawQGmKl3+QuIpUJ0GOQ9asQ\n"
    "89bMUFh9mAYMuCIJLqmSj7czqNQSAtB7wgMrduWmUfLHGRg1pRroSUyT2uThHvkk\n"
    "/inEgLOqFpKPDQ==\n"
    "-----END CERTIFICATE-----";

constexpr auto SERVER_KEY = "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIEowIBAAKCAQEAtGeR0VNWRBIkll4GTiN0ByPnrgWxieuBS8TdrMwVK8Ko30xe\n"
    "WGlQPIV2CElGQxKdIhgGDnQz2Dw0p3LlBsa7SGjWnu1LFNqd+315Fda2WB2WEEPk\n"
    "6VhvuB2oBQfWvgAqQT0KIyQRVwD9sZndSALYbpPNpeTRgIVPE20lBECYmpfSjxt2\n"
    "DOlict6TlTTC+3YcN7Uc8v/hlO+Rb5tpCaG5nfTN0bUqjBvBPjTrPWyjYfwLvdCU\n"
    "Uu+/hQr2Z8poAi1s0qIH6hnTrYsOAFBlZLua7x2y6OI/EjD480i8IIQPsmbKFqeG\n"
    "ZFyqFvC/emMK27xuO01nIRSpAChveLDhNI4eZwIDAQABAoIBAFZy7whdJBCYlRnN\n"
    "Ur4s9RPa29GjftE1no7ddtCwN1DyBhSDNw6s4LsvxUDxRA8uI0hMNNLGUnXDXAQF\n"
    "5cQahXKMcpRT58/Fd0Elflm9u0F8ZAXFUzOSom6bH4HRoNEJqTX79xih1wFXQzrR\n"
    "HZ29Z1ON1lUx0kcBGsSXX86q2bXyTwYlvhgSaEoNc1tkrOfpU92K1WEsyzJpUrJR\n"
    "OP6wGnaNP9R28JLHTgOg9fT0KCmOQFl80fpnJwx4/a4EcewCsTsDDWPetggtYU6U\n"
    "2tknFP2kil5VO2JVBcxBAn8FLEd8+w8W+F/gcMfeXyw0CgfR4SfDaSv8BauKkdUa\n"
    "jkiIbkECgYEA2NPaLOOlL+doKxUmTwcAjL+/FJdXLbq+XcFpsgCeQmzwm3DFokKG\n"
    "pKbsoi2LL9jFYU9F/R/3CIz9DzvaaYuqKwyJeV7Bin9JLqZHaxMOnTx4sdfUbjNQ\n"
    "mA5eMNkxKCWs62lWUMe4/4uXyb6Q8PX2KNnzV7mWic+8iccK9RrMq0cCgYEA1P8t\n"
    "ALH9EnLD8U0VnaEM66n1A6u6/dvVppZ/jUBBg5vvSO+2VzWJ69hZL93BeMWKOFg2\n"
    "fLJQdFt+thlgFjJT97xLoZpy6vc9MIRlv09N4JlWdrYJbKzSkZTN3FiAfFGPUoS0\n"
    "tCRKg+lZotocNpvIj9LEomHI9L9bdewbpvdoQ+ECgYEAqPvS2ZFBOChdItaE2KpY\n"
    "X1lBDkc0hks3+dG3EicxMAu/KSWmoWK/lKsTWQGtrEiA6+ngXfn/iQZ4Ytr+yasz\n"
    "oAFRUunqZIn2+whUOLrNdPWgCtpukMQlV5w7BE8A5I0YSw/5WVOOdnrQfUarp1/J\n"
    "zOSvpfrZu+XOxoH9pDCSuhkCgYBXqN2Al8Arw4fY23y17v4+TyhDZn4C1GLNuMqt\n"
    "+2/7FkYYom9M3P/yTIwIIx/o8IO+RK/ICisKeE1h7HebKwNbxebqj4IUKcTJHvQx\n"
    "FBZdBT6MhMbELyxKNg+zS8k1YOu3bl5gdT8lovf9Cf7qipq0dm3u1oe6Erc1hQKs\n"
    "aM5EgQKBgCumrDZwcLIc+CyTJwpU3xpT5feRCfMpK5p4S9J8Qu3Rgq4qYqOiNTX2\n"
    "W4iPQVHJ3hANCw6uqaeyqH/1ovQLvIX8RvCOXK/kpxuie9oz2X1PVV/3GjWHHrNd\n"
    "J7Hc5FzakjHfbcFOMhZsTAqK3bLa8Z0WoZRZBh5E3iW69wP3P8nH\n"
    "-----END RSA PRIVATE KEY-----";

constexpr auto CLIENT_CERT = "-----BEGIN CERTIFICATE-----\n"
    "MIIDZjCCAk4CFGH1jGrWX7511SW8Wl4e1hT098ebMA0GCSqGSIb3DQEBCwUAMG0x\n"
    "CzAJBgNVBAYTAkNOMRMwEQYDVQQIDApteXByb3ZpbmNlMQ8wDQYDVQQHDAZteWNp\n"
    "dHkxFzAVBgNVBAoMDm15b3JnYW5pemF0aW9uMRAwDgYDVQQLDAdteWdyb3VwMQ0w\n"
    "CwYDVQQDDARteUNBMB4XDTIzMDYxNjA1NDQwMFoXDTI0MDYxNTA1NDQwMFowcjEL\n"
    "MAkGA1UEBhMCQ04xEzARBgNVBAgMCm15cHJvdmluY2UxDzANBgNVBAcMBm15Y2l0\n"
    "eTEXMBUGA1UECgwObXlvcmdhbml6YXRpb24xEDAOBgNVBAsMB215Z3JvdXAxEjAQ\n"
    "BgNVBAMMCWxvY2FsaG9zdDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB\n"
    "ALNTtwIJoRf+y7pr8UJKPODbs3R6LpTzQBHndh+Z6ioRAn9/roW8EcLLeRLGH/xW\n"
    "DLpM1ARcfEnidTCXBsGG+DCv/v4YoDqBWSLbgS6Vuijc5i8VQlXoFS1ynwtSjs8l\n"
    "NUHhArjDtuoRBScF2QT/NDwglLYolXNFqfrYQBIPZ6MfKF+Ht3saJOuwSKbReYjm\n"
    "FAFiry2Lkl9GuAD6sI502+XdtILQSQ+bvoxBsEKYfxG/WI3xsAzOeOtJ2YDPYEmE\n"
    "o/teL0tvkqC6XIIuEgrSO2pX+3ukLyhQRBFkmaLfLI6FdL68lNrTw8suH2mFARoc\n"
    "TwoPVdRMpFcnYMiY/LeEevkCAwEAATANBgkqhkiG9w0BAQsFAAOCAQEAGXWDcoQb\n"
    "1rmhxDLtYPfNGPk/2hympzBttvZI5aZqKhScSEd8YyAPetUiUolCjkzlUmE2aAmq\n"
    "66t8Bu0EkqB2WeWn+w3MB18NNWyp679nZavSeBMxbQ6QZ4YI0xYKgVwtqpwdwg9c\n"
    "TnVBLnhE8PxsKYSrZNOes/KVN4L8+79CF8hUnZG4WnD8DkrXFbVH8/cqm3IQHilC\n"
    "dvOgUWtM2cJcpF7g99cAMWWw+lXbzeYIZ0oQKcymi91nxjqwY0l4zfSJXNuv0FI0\n"
    "N+bcbwQ5UrLxoG/EmdmJ5QKKIe0ixPBt8PTC0BPdtz0holTsGWQDO8vEZIdTHbYz\n"
    "MefUJbHQIfGdWA==\n"
    "-----END CERTIFICATE-----";

constexpr auto CLIENT_KEY = "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIEowIBAAKCAQEAs1O3AgmhF/7LumvxQko84NuzdHoulPNAEed2H5nqKhECf3+u\n"
    "hbwRwst5EsYf/FYMukzUBFx8SeJ1MJcGwYb4MK/+/higOoFZItuBLpW6KNzmLxVC\n"
    "VegVLXKfC1KOzyU1QeECuMO26hEFJwXZBP80PCCUtiiVc0Wp+thAEg9nox8oX4e3\n"
    "exok67BIptF5iOYUAWKvLYuSX0a4APqwjnTb5d20gtBJD5u+jEGwQph/Eb9YjfGw\n"
    "DM5460nZgM9gSYSj+14vS2+SoLpcgi4SCtI7alf7e6QvKFBEEWSZot8sjoV0vryU\n"
    "2tPDyy4faYUBGhxPCg9V1EykVydgyJj8t4R6+QIDAQABAoIBAA2jOC+3e8DPZ77t\n"
    "ppNcAfU3wBYDlLdPKHg0Gg+dLOm2EXKQyzzwaIlf3/1Fg/k0shMY9KbAQdN8nVzs\n"
    "n17oXMeXGtVIGidY3uZ/uvtH97hT1E2QWFMgKzwhk2bRCHtIYRx5eDOE7swuLy4g\n"
    "i7iNj/NipuyTeYoiqiKhf33gHgmRnEIojFyZNytmsvVyyJavR0GbUFvZGIVYQ3Wn\n"
    "u5sLVXWGnSy8TBVHTQxEW7V1ewXY6UE6ouITtqR0GhVwot8N9hSpR8NMQH6OcXkx\n"
    "nvmnQfj7oxASasOmSYIJGzUCm1u6kB0oxIUOXRCW+1r1+NgtHuNKib4NTIIkJ6sx\n"
    "jBsOQ4ECgYEA7l4WzVnC8sKJYAub8IdoMQygv41Q15/OmKLXM/w53r7PWX+e0eqS\n"
    "0JGUTkz+KU47/uj67bd+dlpPU4r5T5oIjmexmvc/NsXuX/VB6ap0VqUkgJAEK40Q\n"
    "y8VVPb3zgbzdvMnsaYKF2uzSw58FUe/hvvTJHXdcOoJZ/zd4z2A2K1UCgYEAwJeW\n"
    "y3VKrMmErnRJxVW8c418Y/U2zo1MHQJXMnbA69mYSMKanhnEL/QHyQF1U/L6JpS9\n"
    "BRH+nKadu0zPeXwT2+n/xJToQQA+oViTzWVjdc+YxLHQxGzCsqFlYb5oJ9KVcOHd\n"
    "pK03M0xGMwK51rU495zNkES+j6oIdbw1lKe5ORUCgYEAuoNEtEmYEPvHIi/zRLGv\n"
    "BTIsVbXtm8qfjS9d1H93iKMk+5KwYvB5EFnZAauc9BUTPNJwBbGecl0X1PbZPT/5\n"
    "kfPxNKBiBim567usZ3nIrkNp3G7T1H/8tHUjzbvj3ZA6sI4PPj+zHSpRgF/dec+J\n"
    "hDFlbHkI4X89jEWLcjiGKkECgYBP5BInoCYz+vxRKegYNfKQtJZvGJ99m1uBhSEK\n"
    "y/xHWeIz/JYLE4Ewqzg3h6VWB6sBKh4m5koKTYuM0NYX/QJ37V+t+l9F54YThBz7\n"
    "zR0vF8i1f1WsxbkWRKE7pLKsIkzfoLJCx5/oZbGRI4ZXrKFPBfq35+xsyAnuXP6V\n"
    "BeZt6QKBgHKZXQSVcoZc+2f8Ev/VppvkDon23Z/49iuJ8WDQWjziWWSzLSg7Q/c/\n"
    "6TvCDsSH5OL5/IenRo3szVg63QFH/0hTT7SUVU6775bT3jfKQAxoHT2Nolp8bl9P\n"
    "rqtIHR7De/4WKI8TWL6ismjqd5WOcD21AlMBiLQr1KWlAFa2Vn6x\n"
    "-----END RSA PRIVATE KEY-----";

constexpr std::string_view MESSAGE = "hello world\r\n";

TEST_CASE("ssl stream network connection", "[net]") {
    asyncio::run([]() -> zero::async::coroutine::Task<void> {
        SECTION("mutual authentication") {
            const auto context = asyncio::net::ssl::newContext(
                {
                    .ca = std::string{CA_CERT},
                    .cert = std::string{SERVER_CERT},
                    .privateKey = std::string{SERVER_KEY},
                    .server = true
                }
            );
            REQUIRE(context);

            auto listener = asyncio::net::ssl::stream::listen(*context, "127.0.0.1", 30000);
            REQUIRE(listener);

            co_await allSettled(
                [](auto l) -> zero::async::coroutine::Task<void> {
                    auto buffer = co_await l.accept();
                    REQUIRE(buffer);

                    const auto localAddress = buffer->localAddress();
                    REQUIRE(localAddress);
                    REQUIRE(fmt::to_string(*localAddress) == "variant(127.0.0.1:30000)");

                    const auto remoteAddress = buffer->remoteAddress();
                    REQUIRE(remoteAddress);
                    REQUIRE(fmt::to_string(*remoteAddress).find("127.0.0.1") != std::string::npos);

                    auto result = co_await buffer->writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(result);

                    result = co_await buffer->flush();
                    REQUIRE(result);

                    const auto line = co_await buffer->readLine();
                    REQUIRE(line);
                    REQUIRE(*line == zero::strings::trim(MESSAGE));
                }(std::move(*listener)),
                []() -> zero::async::coroutine::Task<void> {
                    const auto ctx = asyncio::net::ssl::newContext(
                        {
                            .ca = std::string{CA_CERT},
                            .cert = std::string{CLIENT_CERT},
                            .privateKey = std::string{CLIENT_KEY}
                        }
                    );
                    REQUIRE(ctx);

                    auto buffer = co_await asyncio::net::ssl::stream::connect(
                        *ctx,
                        "localhost",
                        30000
                    );
                    REQUIRE(buffer);

                    const auto localAddress = buffer->localAddress();
                    REQUIRE(localAddress);
                    REQUIRE(fmt::to_string(*localAddress).find("127.0.0.1") != std::string::npos);

                    const auto remoteAddress = buffer->remoteAddress();
                    REQUIRE(remoteAddress);
                    REQUIRE(fmt::to_string(*remoteAddress) == "variant(127.0.0.1:30000)");

                    const auto line = co_await buffer->readLine();
                    REQUIRE(line);
                    REQUIRE(*line == zero::strings::trim(MESSAGE));

                    auto result = co_await buffer->writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(result);

                    result = co_await buffer->flush();
                    REQUIRE(result);
                }()
            );
        }

        SECTION("verify server") {
            const auto context = asyncio::net::ssl::newContext(
                {
                    .ca = std::string{CA_CERT},
                    .cert = std::string{SERVER_CERT},
                    .privateKey = std::string{SERVER_KEY},
                    .insecure = true,
                    .server = true
                }
            );
            REQUIRE(context);

            auto listener = asyncio::net::ssl::stream::listen(*context, "127.0.0.1", 30000);
            REQUIRE(listener);

            co_await allSettled(
                [](auto l) -> zero::async::coroutine::Task<void> {
                    auto buffer = co_await l.accept();
                    REQUIRE(buffer);

                    const auto localAddress = buffer->localAddress();
                    REQUIRE(localAddress);
                    REQUIRE(fmt::to_string(*localAddress) == "variant(127.0.0.1:30000)");

                    const auto remoteAddress = buffer->remoteAddress();
                    REQUIRE(remoteAddress);
                    REQUIRE(fmt::to_string(*remoteAddress).find("127.0.0.1") != std::string::npos);

                    auto result = co_await buffer->writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(result);

                    result = co_await buffer->flush();
                    REQUIRE(result);

                    const auto line = co_await buffer->readLine();
                    REQUIRE(line);
                    REQUIRE(*line == zero::strings::trim(MESSAGE));
                }(std::move(*listener)),
                []() -> zero::async::coroutine::Task<void> {
                    const auto ctx = asyncio::net::ssl::newContext({.ca = std::string{CA_CERT}});
                    REQUIRE(ctx);

                    auto buffer = co_await asyncio::net::ssl::stream::connect(
                        *ctx,
                        "localhost",
                        30000
                    );
                    REQUIRE(buffer);

                    const auto localAddress = buffer->localAddress();
                    REQUIRE(localAddress);
                    REQUIRE(fmt::to_string(*localAddress).find("127.0.0.1") != std::string::npos);

                    const auto remoteAddress = buffer->remoteAddress();
                    REQUIRE(remoteAddress);
                    REQUIRE(fmt::to_string(*remoteAddress) == "variant(127.0.0.1:30000)");

                    const auto line = co_await buffer->readLine();
                    REQUIRE(line);
                    REQUIRE(*line == zero::strings::trim(MESSAGE));

                    auto result = co_await buffer->writeAll(std::as_bytes(std::span{MESSAGE}));
                    REQUIRE(result);

                    result = co_await buffer->flush();
                    REQUIRE(result);
                }()
            );
        }
    });
}

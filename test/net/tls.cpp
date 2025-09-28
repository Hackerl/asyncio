#include <catch_extensions.h>
#include <asyncio/net/tls.h>
#include <asyncio/net/stream.h>

constexpr auto CA_CERT = R"(-----BEGIN CERTIFICATE-----
MIIDTTCCAjUCFCvK20SUCJA9JqCcIpXa4ATR9O+BMA0GCSqGSIb3DQEBCwUAMGMx
CzAJBgNVBAYTAkNOMREwDwYDVQQIDAhwcm92aW5jZTENMAsGA1UEBwwEY2l0eTEV
MBMGA1UECgwMb3JnYW5pemF0aW9uMQ4wDAYDVQQLDAVncm91cDELMAkGA1UEAwwC
Q0EwHhcNMjQwNzAxMDcwOTQ2WhcNMzQwNjI5MDcwOTQ2WjBjMQswCQYDVQQGEwJD
TjERMA8GA1UECAwIcHJvdmluY2UxDTALBgNVBAcMBGNpdHkxFTATBgNVBAoMDG9y
Z2FuaXphdGlvbjEOMAwGA1UECwwFZ3JvdXAxCzAJBgNVBAMMAkNBMIIBIjANBgkq
hkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAlezRUr5SWQHwbulSj/LXrgsMTi7e8jGU
yjMnaKRXDkXQD2tgWaJUd4EHFF51lLVsTCuNreCdJqDooyOEZLGtNmyGvCoJ6Ntm
4VfD0MYWh+lrYWZ7g7CQ2iOsNcarrB+PIzv2aD73wER2ZSQtO0sD7HaBhLpbKMW3
gZChIT0rjUf1nepvcoviLtgFVmIaWxis0BvQ91Y4F2ctYIRFeMrdoNJ1gSNdd5RL
WclvaNXyMYIeJoRrlZRufaWEr9UQMpfVwIPQwhn8QRSP9mrMONHUeLSvT47yHUAO
eJgsQbrak666xcX8rHKmRRolS6i/y2CWb07wm8u2DgByXAEaqWTQdwIDAQABMA0G
CSqGSIb3DQEBCwUAA4IBAQB74c4gh68zkjvX08Q0JBmqDrUOqQc6fNAJ+HM3aUtO
kHt0qXlntmenv0TqPj6kzr7q2j8ewPbppbmsqPTULEJUqIJdrWayXvRJJ2KpbYjo
Qa4/zgiRT3hN6cDIoS2VFbTPBIR5reOyKGe4hU6UF5kVo5F6aSEiU7bhwEqv28cD
2RafSS23ubvd2R4CIgyn9WdT7UG6OcW8TLPxPJoIaH6y97z6nwCB1IitHAYgpLR3
qhglQgrFE59S2EbeX7k9yZgrhmnH30y+rMxQley2UpuZ0cwq0Urk/oHIDQ98PH0G
AAvp/KZkANu0m1tD1VBcbWF5o502z4+7Z+loklS4iDPx
-----END CERTIFICATE-----)";

constexpr auto SERVER_CERT = R"(-----BEGIN CERTIFICATE-----
MIIDVDCCAjwCFHaLVGbQ4Fhrr5vqKSo8FBT3CRGGMA0GCSqGSIb3DQEBCwUAMGMx
CzAJBgNVBAYTAkNOMREwDwYDVQQIDAhwcm92aW5jZTENMAsGA1UEBwwEY2l0eTEV
MBMGA1UECgwMb3JnYW5pemF0aW9uMQ4wDAYDVQQLDAVncm91cDELMAkGA1UEAwwC
Q0EwHhcNMjQwNzAxMDcwOTQ2WhcNMzQwNjI5MDcwOTQ2WjBqMQswCQYDVQQGEwJD
TjERMA8GA1UECAwIcHJvdmluY2UxDTALBgNVBAcMBGNpdHkxFTATBgNVBAoMDG9y
Z2FuaXphdGlvbjEOMAwGA1UECwwFZ3JvdXAxEjAQBgNVBAMMCWxvY2FsaG9zdDCC
ASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAJsXNpubaCZfLfJzwdKxoTYC
nGkrAaZADz8uDk8SUQI50f6SUMhXMzPNk2io+fbOUOZDPR1diPV4VsdOMTMCSlQS
Dss4XzCrl4YpZ9E3BCNgqUxEAvn8coNhw5G1mmMTwtdIhgkPDirLGYIPLb1yKejo
W3ocGQPrLj3k7/yt+TsEL6gS4miJj8NHnz3vymmM7JrJzyrUqq9W1CFpilqaakR4
BoSHOfgApdMbaGtS/U0bHFyVzVOUJnOksr/PgX6zFKniawg3wl+7YqfxjSFhXCSG
JB1/kCtc+tcibfBaNXk2cxSWmyI6i4L9ernXO+wYeFpppA/HzTIa5DtRYHtaGmsC
AwEAATANBgkqhkiG9w0BAQsFAAOCAQEATncbocQR6QC36bZawiBi853+fJdhqjpV
sXvndkjENwGVBE96izkYfZ/Ashf57yUalARQ59JtTB1RXR/jqVbU0T+xHWYXzUxw
EMnRHBo6alkBi67Od7kz1SYyw9C54vPoT5qIUT4CXIm376mwsJFoge1zFDV/TW9N
w5jX4Qipo7iFEn+4Dhio+ayJM/SY0qx3I6PHmJqRbZXPdJELfwvtZnKqIqXsgsFw
9pEdHj7p8pXUYP2HiVooGVEy/VSDgPshnwnZaNElACIUy2Ao+RMC6oeqrEAeQn3f
2bhrfqj73zv0xjvd2dV2aPdziXNkqneOT06T3e4WgiJ78YKtRNd3wg==
-----END CERTIFICATE-----)";

constexpr auto SERVER_KEY = R"(-----BEGIN PRIVATE KEY-----
MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCbFzabm2gmXy3y
c8HSsaE2ApxpKwGmQA8/Lg5PElECOdH+klDIVzMzzZNoqPn2zlDmQz0dXYj1eFbH
TjEzAkpUEg7LOF8wq5eGKWfRNwQjYKlMRAL5/HKDYcORtZpjE8LXSIYJDw4qyxmC
Dy29cino6Ft6HBkD6y495O/8rfk7BC+oEuJoiY/DR58978ppjOyayc8q1KqvVtQh
aYpammpEeAaEhzn4AKXTG2hrUv1NGxxclc1TlCZzpLK/z4F+sxSp4msIN8Jfu2Kn
8Y0hYVwkhiQdf5ArXPrXIm3wWjV5NnMUlpsiOouC/Xq51zvsGHhaaaQPx80yGuQ7
UWB7WhprAgMBAAECggEAGCGvMtx7gtJMfpfZu27oJqnsAcaEpdmnMFgk4dN3x2Di
do9NrTPkQ6s5GeUw/7Yaiw7rSNEaU8I7E8fsNS5Qt8QXiTIRnavAEqAJXK8IIHkU
iUlhLS9FTNa+whf/wxV6u69igQZWrfyW0NCLzW52FvIQnxoVdQajAKkerl0OP/Wo
j0c4erKnvwmBRvZHcg/dwz0vrlGU56gmGsI/rEOu/uEPiHe1YivM5hsg8yCRScxO
gZ8usYFmr43HVH1cDoLO7pTpql6TUq7iPsUBZ8ZsttZxgQ9hF90xXLqM6dloMhkP
1zHPQOtOakFIN66Wu0BNoitf7sq0A/I16OeSqQMkAQKBgQDYzxGjXzbf0LT9GnXR
prg1jurg1VU53s/vzrKpkkwQL0hnsgu4WUsJC1A/zbrkYCRNrPKWXtoF0Q8NvOHy
GECeWW5j0k+qpaGi6ca0hE+suAM6IxI62vDQ1bpNBMYMvmrfzD19vtuMm+34ijfm
hapnNSesKCNXO0pZoMHndILCXQKBgQC3IBtLTmYsjsrcGgxa0+tY18exy0pWvdsq
aLsI2m5WRPvYJ/2z2VL1PBPRfwdiTjq/4ExPtWTmot/ICLOSUMkFJCwwd5L+7OyB
Abv61i2OSZ7SW+CxhGpTZYaud3hkbJI4ob76X8I5zxoGHJKnXL8/8gTceD7mmbzh
OFj03IUTZwKBgHd/mj8rybkO9dLTdMD7XWjXvwnxS6o77uxERyFDq3z4MrZE4kTX
oo33mGGyaSJbA2d0vbi3yv+NvPhbdUXUrDkbHccTMiniZPbAV29Dxg5y1gSDNBcQ
ec8BHlA5I1f+1DVKA1J9kdEsWLu2ox0B62w7VgSDkwcPfOltu7Jw+2lZAoGATmOq
C2R2DGDbqbhkzYSl3BQSYaNaISOIa/EFD1choEBLZk5IJfLDtkKPYUB1UrmWIIjN
YdmOZcQW8zP2Wo3GXzMKz3yAKiwVWWv6ofeI7L3LBNVbq+G4+hYdvxVZLfPj6+Yh
MGBJbiGfTDpy9L2ZCLB7MD0w/9mTpv1N5xN2y8UCgYBz9afIh9uqXcMAK+wnLPVc
50m78p0g6W0i1UMqzNa0Hn03DkAIDsZdBwL5S1EszZqTuGAWOE1SI3yTEyWWUYus
hRtEqBlavZs7O3Vfmx5nrEaw54becy537a59C+aTA/FH0FxPYi0SupydCsued70Q
IlQYsvqGkLQYa89tFQZxHA==
-----END PRIVATE KEY-----)";

constexpr auto CLIENT_CERT = R"(-----BEGIN CERTIFICATE-----
MIIDUTCCAjkCFHaLVGbQ4Fhrr5vqKSo8FBT3CRGHMA0GCSqGSIb3DQEBCwUAMGMx
CzAJBgNVBAYTAkNOMREwDwYDVQQIDAhwcm92aW5jZTENMAsGA1UEBwwEY2l0eTEV
MBMGA1UECgwMb3JnYW5pemF0aW9uMQ4wDAYDVQQLDAVncm91cDELMAkGA1UEAwwC
Q0EwHhcNMjQwNzAxMDcwOTQ3WhcNMzQwNjI5MDcwOTQ3WjBnMQswCQYDVQQGEwJD
TjERMA8GA1UECAwIcHJvdmluY2UxDTALBgNVBAcMBGNpdHkxFTATBgNVBAoMDG9y
Z2FuaXphdGlvbjEOMAwGA1UECwwFZ3JvdXAxDzANBgNVBAMMBmNsaWVudDCCASIw
DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALad3rfrN19aJzx7dKeqI/lyh8kq
SArIj7m8EKmof7pleY4mXVly3Bu968WLtrQ7ThR7LoMhSZU3PETOWmPnXXkztBL9
1A4LeORpEQ8gI5j7RSRf4KWNbc/YaPFWh67hmIVL6c23dZDjCMr+pStpxyEWK9iN
LKAfeu8zxeWJRfWU0GpEUXv+iAan/ZNTp9tD0MKxVXNHJVilsQP6muIleroxG68H
7w68JO9HV/hJj0cEOPHm3wHEELL+FERhrk/iHgk0EhdT6kfpRX9HMCz3SzntK6Az
NZPRclh1iOzGe3+TRRaeq2+CzlT0lh5CRlINEj/KEDuKfxa93l2gdWyKmkkCAwEA
ATANBgkqhkiG9w0BAQsFAAOCAQEAN0+sMfhR0uM/JxtK09Ur9CZ4m4ZUb0TH+9vI
udrcGlOGBr6NGPZYIFADPzqeGG/OcExW1UptqT6dgAWW+yD7PG4CUX02C/nBxVh2
RTvT02kmuYTMnHaRKlTOsSvKtWMFZ6RKa9/GLxyUpdIzL87H/M4l5vk1QvtdfM98
H0tCWi1WQLhDNXt0xxi+3RUL0syzraqpMvaLNPTIyWKYyEGF/QrhkxXnY4FOZRrW
VibLVnnMjsElEQCztj+VYfoFWnXJsw37sTeMAo//uTF29lyziqMaybfphMVu2+F2
KA/lhyr+GODHSYcB9R2zCJ53ebgUx29HBhk2QCoDioLaHkTMOw==
-----END CERTIFICATE-----)";

constexpr auto CLIENT_KEY = R"(-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQC2nd636zdfWic8
e3SnqiP5cofJKkgKyI+5vBCpqH+6ZXmOJl1ZctwbvevFi7a0O04Uey6DIUmVNzxE
zlpj5115M7QS/dQOC3jkaREPICOY+0UkX+CljW3P2GjxVoeu4ZiFS+nNt3WQ4wjK
/qUracchFivYjSygH3rvM8XliUX1lNBqRFF7/ogGp/2TU6fbQ9DCsVVzRyVYpbED
+priJXq6MRuvB+8OvCTvR1f4SY9HBDjx5t8BxBCy/hREYa5P4h4JNBIXU+pH6UV/
RzAs90s57SugMzWT0XJYdYjsxnt/k0UWnqtvgs5U9JYeQkZSDRI/yhA7in8Wvd5d
oHVsippJAgMBAAECggEAH4D4rS3s7Yw1/J8nrs0RS7VQa4nZx9J1yQsCrOIett9O
qxE/RXElO7v18qx5pnocrDfb/E8/zHgs84nN2jJN09zxkd0sOggMoQrnIH7g5UFj
2jSYIrdVXGfvIsNUwPy2PTitawndRFOGbs6jW+vxqGmUwtUH/8+ue5mo6EJYSVdJ
XmDeOtDJ1kxlG61cAkke3wFJINRPtZ/RKAP99xVbUzdn60Ho5r7e1YjPwLsnhQlD
tqT4/kXQHNN50gPxpKRa3rVaCqDoNVpo+9xhAAB1+ZGH0VLFFLhfh1FajNqL0e3/
Jji/KdNh3GErTaXp+avrCjMfTZtvLEIqA813shoIoQKBgQD9C/eF4QjY8/fJAPqi
VRWuP2yAXaN3murYhl+C3XEM/pw2JbcxdkROM0Q1TV6DRgSAZPtlE5ojmhyaUdNm
4sLN9PHiUmiAnzqlmvbE7MYi1q2MDsbSQ/7nWBm7lRR563lxs32mXoTCuzQsU+sE
iCgwbCE1GVum65Ad6KqJWChXHQKBgQC4v3pLLqiXxYQn49n1AL41Kum26/Gl/60y
bouEF0mLkCLXcAEriM3vuROis120XyzmJ7xuwwiWULoy83eCDtZp9V5h8jqky89Q
LtLSflYQ7dV80InQgdSHqEIJnPUmS2BPpW35XqUcf188wgaoMGDEhjenLKCVmwyk
SjkAbDLsHQKBgD+fqKayxCS4gs65PktelUrwi3ljEdzayL2UW6NtxiT8R46dwQfg
sp/u5/F2+JKNIkieG094tELLVvG+LScrUMu0ELVU/w2H+5jz86Pj3JWZ4JzcgkUP
76F/V0kA3Nsix7A87xTLoxnLM/0JRIgpK+Gw7lCJha/cfqnmfCp4TfxJAoGAYy13
i1C69O71qSxqX1pMweINoUM8cG63HbG2d+zAcImqCpl4J1TDqQNkBR3hGelWAyAl
yhgtTfR8YMnOyCUK+crdJhuRW9KFsnfQeWuU7HWg++Y/dY2c+E5dVSfLewlP5LHc
PdiPLqM6DzXhuPxvllCvI7GTa3zW5oNp4k4zcAkCgYEAu4FdXOhAzP3Md4sRqPzk
Et4LoWkYWPBHwIiBqwh4VIA1Jri8N2vlqLRJyxSZniMAzIGTx1Irb/EW4BxsxUoV
7TiD2Qn5X77L8AsVbDeZPay/I5vkxdNGWtwpgvvFD8c0SVRFjaiEqp6cShR9VZFz
FReHT5LzsIm40VPPdsITh6c=
-----END PRIVATE KEY-----)";

ASYNC_TEST_CASE("tls stream", "[net::tls]") {
    auto ca = asyncio::net::tls::Certificate::load(CA_CERT);
    REQUIRE(ca);

    auto serverCert = asyncio::net::tls::Certificate::load(SERVER_CERT);
    REQUIRE(serverCert);

    auto serverKey = asyncio::net::tls::PrivateKey::load(SERVER_KEY);
    REQUIRE(serverKey);

    auto serverContext = asyncio::net::tls::ServerConfig{}
                         .verifyClient(true)
                         .rootCAs({*ca})
                         .certKeyPairs({{*std::move(serverCert), *std::move(serverKey)}})
                         .build();
    REQUIRE(serverContext);

    auto clientCert = asyncio::net::tls::Certificate::load(CLIENT_CERT);
    REQUIRE(clientCert);

    auto clientKey = asyncio::net::tls::PrivateKey::load(CLIENT_KEY);
    REQUIRE(clientKey);

    auto clientContext = asyncio::net::tls::ClientConfig{}
                         .rootCAs({*ca})
                         .certKeyPairs({{*std::move(clientCert), *std::move(clientKey)}})
                         .build();
    REQUIRE(clientContext);

    auto listener = asyncio::net::TCPListener::listen("127.0.0.1", 0);
    REQUIRE(listener);

    const auto address = listener->address();
    REQUIRE(address);

    auto result = co_await all(
        listener->accept().andThen([&](asyncio::net::TCPStream &&stream) {
            return asyncio::net::tls::accept(std::move(stream), *std::move(serverContext));
        }),
        asyncio::net::TCPStream::connect(*address).andThen([&](asyncio::net::TCPStream &&stream) {
            return asyncio::net::tls::connect(std::move(stream), *std::move(clientContext));
        })
    );
    REQUIRE(result);

    auto &server = result->at(0);
    auto &client = result->at(1);

    const auto input = GENERATE(take(1, randomBytes(1, 102400)));

    SECTION("read") {
        auto task = server.writeAll(input);

        std::vector<std::byte> data;
        data.resize(input.size());

        REQUIRE(co_await client.readExactly(data));
        REQUIRE(co_await task);
        REQUIRE(data == input);
    }

    SECTION("write") {
        std::vector<std::byte> data;
        data.resize(input.size());

        auto task = server.readExactly(data);

        REQUIRE(co_await client.writeAll(input));
        REQUIRE(co_await task);
        REQUIRE(data == input);
    }

    SECTION("shutdown") {
        {
            auto task = client.shutdown();

            std::array<std::byte, 1024> data{};
            REQUIRE(co_await server.read(data) == 0);
            REQUIRE(co_await task);
        }

        {
            auto task = server.writeAll(input);

            std::vector<std::byte> data;
            data.resize(input.size());

            REQUIRE(co_await client.readExactly(data));
            REQUIRE(co_await task);
            REQUIRE(data == input);
        }

        {
            auto task = server.shutdown();

            std::array<std::byte, 1024> data{};
            REQUIRE(co_await client.read(data) == 0);
            REQUIRE(co_await task);
        }
    }

    SECTION("close") {
        auto task = client.close();

        std::array<std::byte, 1024> data{};
        REQUIRE(co_await server.read(data) == 0);
        REQUIRE(co_await server.shutdown());
        REQUIRE(co_await task);
    }
}

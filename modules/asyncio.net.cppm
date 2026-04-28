module;

#include <asyncio/net/dgram.h>
#include <asyncio/net/dns.h>
#include <asyncio/net/net.h>
#include <asyncio/net/stream.h>
#include <asyncio/net/tls.h>

export module asyncio:net;

export namespace asyncio::net {
    using asyncio::net::UDPSocket;

    using asyncio::net::getAddressInfo;
    using asyncio::net::lookupIP;

    using asyncio::net::IPv4;
    using asyncio::net::IPv6;
    using asyncio::net::IP;
    using asyncio::net::LOCALHOST_IPV4;
    using asyncio::net::BROADCAST_IPV4;
    using asyncio::net::UNSPECIFIED_IPV4;
    using asyncio::net::LOCALHOST_IPV6;
    using asyncio::net::UNSPECIFIED_IPV6;
    using asyncio::net::IPv4Address;
    using asyncio::net::IPv6Address;
    using asyncio::net::UnixAddress;
    using asyncio::net::IPAddress;
    using asyncio::net::Address;
    using asyncio::net::SocketAddress;
    using asyncio::net::operator==;
    using asyncio::net::ipAddressFrom;
    using asyncio::net::addressFrom;
    using asyncio::net::socketAddressFrom;
    using asyncio::net::IEndpoint;
    using asyncio::net::ISocket;
    using asyncio::net::copyBidirectional;
    using asyncio::net::ParseAddressError;

    using asyncio::net::TCPStream;
    using asyncio::net::TCPListener;
    #ifdef _WIN32
    using asyncio::net::NamedPipeStream;
    using asyncio::net::NamedPipeListener;
    #else
    using asyncio::net::UnixStream;
    using asyncio::net::UnixListener;
    #endif

    namespace tls {
        using asyncio::net::tls::openSSLError;
        #ifdef __linux__
        using asyncio::net::tls::systemCABundle;
        #endif
        using asyncio::net::tls::expected;
        using asyncio::net::tls::Version;
        using asyncio::net::tls::Certificate;
        using asyncio::net::tls::PrivateKey;
        using asyncio::net::tls::CertKeyPair;
        using asyncio::net::tls::Context;
        using asyncio::net::tls::ClientConfig;
        using asyncio::net::tls::ServerConfig;
        using asyncio::net::tls::Config;
        using asyncio::net::tls::TLS;
        using asyncio::net::tls::connect;
        using asyncio::net::tls::accept;
        using asyncio::net::tls::OpenSSLError;
        using asyncio::net::tls::TLSError;
    }
}

export namespace fmt {
    using fmt::formatter;
}

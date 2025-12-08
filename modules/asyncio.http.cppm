module;

#include <asyncio/http/request.h>
#include <asyncio/http/url.h>
#include <asyncio/http/websocket.h>

export module asyncio:http;

export namespace asyncio::http {
    using asyncio::http::Connection;
    using asyncio::http::Requests;
    using asyncio::http::Response;
    using asyncio::http::TLSConfig;
    using asyncio::http::Options;
    using asyncio::http::CURLError;
    using asyncio::http::CURLMError;

    using asyncio::http::urlEscape;
    using asyncio::http::urlUnescape;
    using asyncio::http::URL;
    using asyncio::http::operator<=>;
    using asyncio::http::operator==;
    
    namespace ws {
        using asyncio::http::ws::CloseCode;
        using asyncio::http::ws::CloseCodeCategory;
        using asyncio::http::ws::make_error_code;
        using asyncio::http::ws::Opcode;
        using asyncio::http::ws::InternalMessage;
        using asyncio::http::ws::Message;
        using asyncio::http::ws::Header;
        using asyncio::http::ws::Frame;
        using asyncio::http::ws::Compressor;
        using asyncio::http::ws::Decompressor;
        using asyncio::http::ws::DeflateConfig;
        using asyncio::http::ws::DeflateExtension;
        using asyncio::http::ws::WebSocket;
    }
}

export namespace zero {
    using zero::scan;
}

export namespace fmt {
    using fmt::formatter;
}

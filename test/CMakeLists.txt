find_package(Catch2 CONFIG REQUIRED)

add_executable(
        asyncio_test
        io.cpp
        fs.cpp
        time.cpp
        poll.cpp
        thread.cpp
        buffer.cpp
        binary.cpp
        promise.cpp
        channel.cpp
        event_loop.cpp
        net/net.cpp
        net/dns.cpp
        net/tls.cpp
        net/dgram.cpp
        net/stream.cpp
        http/url.cpp
        http/request.cpp
        sync/mutex.cpp
        sync/event.cpp
        sync/condition.cpp
        $<$<NOT:$<PLATFORM_ID:Windows>>:signal.cpp>
)

target_link_libraries(asyncio_test PRIVATE asyncio Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(asyncio_test)

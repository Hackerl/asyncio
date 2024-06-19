find_package(Catch2 CONFIG REQUIRED)

add_executable(
        asyncio_test
        io.cpp
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
        net/dgram.cpp
        net/stream.cpp
        $<$<NOT:$<PLATFORM_ID:Windows>>:signal.cpp>
#        sync/mutex.cpp
#        sync/event.cpp
#        sync/condition.cpp
#        fs/file.cpp
#        fs/pipe.cpp
#        net/net.cpp
#        net/dns.cpp
#        net/ssl.cpp
#        net/dgram.cpp
#        net/stream.cpp
#        http/url.cpp
#        http/request.cpp
#        $<$<PLATFORM_ID:Linux,Android>:fs/aio.cpp>
#        $<$<PLATFORM_ID:Linux,Darwin>:fs/posix.cpp>
#        $<$<PLATFORM_ID:Windows>:main.cpp fs/iocp.cpp>
)

target_link_libraries(asyncio_test PRIVATE asyncio Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(asyncio_test)
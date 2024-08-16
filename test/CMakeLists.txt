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
        process.cpp
        event_loop.cpp
        task/error.cpp
        task/exception.cpp
        net/net.cpp
        net/dns.cpp
        net/tls.cpp
        net/dgram.cpp
        net/stream.cpp
        http/url.cpp
        http/request.cpp
        http/websocket.cpp
        sync/mutex.cpp
        sync/event.cpp
        sync/condition.cpp
        $<$<NOT:$<PLATFORM_ID:Windows>>:signal.cpp>
)

target_link_libraries(asyncio_test PRIVATE asyncio Catch2::Catch2WithMain)

include(Catch)

# Catch2 v3.7.0 will inherit the PATH environment variable, so we don't need to set it manually.
catch_discover_tests(
        asyncio_test
        DL_PATHS
        $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${BUILD_SHARED_LIBS}>>:$ENV{PATH};$<TARGET_FILE_DIR:asyncio>>
)

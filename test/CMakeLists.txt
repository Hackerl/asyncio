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
        $<$<PLATFORM_ID:Windows,Darwin,Linux,Android>:process.cpp>
)

if (MINGW)
    target_compile_options(asyncio_test PRIVATE -Wa,-mbig-obj)
endif ()

target_include_directories(asyncio_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asyncio_test PRIVATE asyncio Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(asyncio_test)
